#include "net.h"
#include "io.h"
#include "net_ipv4.h"
#include "net_dhcp.h"
#include "net_tcp.h"

/* The RTL8139 is deliberately opt-in by PCI device ID. Unrecognized network
 * hardware remains untouched. A single polling driver avoids changing PIC/IDT.
 * MayKernel's boot64.S maps early physical RAM 1:1; the statically allocated
 * DMA buffers and PCI BAR are verified below to fit their 32-bit hardware ABI.
 *
 * References: RTL8139 datasheet (CONFIG1, RBSTART, CMD, CAPR, RCR, TSD/TSAD)
 * and OSDev RTL8139 bring-up guide. This is QEMU experimental code, NOT a
 * full Ethernet/IP driver or production-grade hardware DMA isolation. */
#define RTL_VENDOR 0x10ecu
#define RTL_DEVICE 0x8139u
#define RTL_RING_BYTES 8192u
#define RTL_RX_ALLOC (RTL_RING_BYTES + 16u + 2048u)
#define RTL_TX_ALLOC 1536u

enum {
    RTL_TSD0 = 0x10, RTL_TSAD0 = 0x20,
    RTL_RBSTART = 0x30, RTL_CMD = 0x37, RTL_CAPR = 0x38,
    RTL_IMR = 0x3c, RTL_ISR = 0x3e, RTL_RCR = 0x44,
    RTL_CONFIG1 = 0x52
};
static MayNetInfo nic;
static u32 rx_cursor, next_tx;
static u8 rx_ring[RTL_RX_ALLOC] __attribute__((aligned(256)));
static u8 tx_frames[4][RTL_TX_ALLOC] __attribute__((aligned(16)));
static u8 ipv4_receive[RTL_TX_ALLOC];
static u8 qemu_gateway_mac[6];
static int qemu_gateway_mac_valid, udp_probe_active, dns_probe_active;
static MayDhcpClient dhcp;
static int dhcp_request_pending;
static u32 tcp_next_seq,tcp_remote_seq,tcp_captured;
static int tcp_state,tcp_pending_ack,tcp_pending_get;
static u8 tcp_capture[256];
static const u8 qemu_local_ip[4] = {10, 0, 2, 15};
static const u8 qemu_gateway_ip[4] = {10, 0, 2, 2};

static u32 pci_read(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = 0x80000000u |
        ((u32)bus << 16) | ((u32)slot << 11) |
        ((u32)func << 8) | ((u32)offset & 0xfcu);
    outl(0xcf8, address);
    return inl(0xcfc);
}
static void pci_write(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = 0x80000000u |
        ((u32)bus << 16) | ((u32)slot << 11) |
        ((u32)func << 8) | ((u32)offset & 0xfcu);
    outl(0xcf8, address);
    outl(0xcfc, value);
}
static u8 ring_byte(u32 address) {
    return rx_ring[address & (RTL_RING_BYTES - 1u)];
}
static int address_32bit(const void *pointer) {
    uptr value = (uptr)pointer;
    return value != 0 && value <= 0xffffffffu;
}
static int rtl_setup(u8 bus, u8 slot, u8 func) {
    u32 bar = pci_read(bus, slot, func, 0x10);
    if (!(bar & 1u)) {
        serial("MayOS: RTL8139 PCI BAR0 is not I/O; refusing init\n");
        return 0;
    }
    u32 base = bar & ~3u;
    if (!base || base > 0x10000u - 0x60u ||
        !address_32bit(rx_ring) ||
        !address_32bit(&rx_ring[RTL_RX_ALLOC - 1]) ||
        !address_32bit(tx_frames) ||
        !address_32bit(&tx_frames[3][RTL_TX_ALLOC - 1])) {
        serial("MayOS: RTL8139 invalid I/O BAR or DMA memory\n");
        return 0;
    }
    u32 old = pci_read(bus, slot, func, 0x04);
    pci_write(bus, slot, func, 0x04, old | 0x5u); /* PCI I/O + bus master */
    if ((pci_read(bus, slot, func, 0x04) & 0x5u) != 0x5u) {
        serial("MayOS: RTL8139 PCI bus mastering unavailable\n");
        return 0;
    }

    u16 io = (u16)base;
    outb(io + RTL_CONFIG1, 0x00);
    outb(io + RTL_CMD, 0x10); /* bounded software reset */
    u32 remaining = 100000u;
    while ((inb(io + RTL_CMD) & 0x10u) && remaining) remaining--;
    if (!remaining) {
        serial("MayOS: RTL8139 reset timeout\n");
        return 0;
    }

    memset(rx_ring, 0, sizeof(rx_ring));
    memset(tx_frames, 0, sizeof(tx_frames));
    nic.io_base = io;
    for (int i = 0; i < 6; i++) nic.mac[i] = inb(io + i);
    int any = 0, all_ff = 1;
    for (int i = 0; i < 6; i++) {
        if (nic.mac[i]) any = 1;
        if (nic.mac[i] != 0xff) all_ff = 0;
    }
    if (!any || all_ff || (nic.mac[0] & 1u)) {
        serial("MayOS: RTL8139 invalid hardware MAC\n");
        return 0;
    }
    rx_cursor = next_tx = 0;
    qemu_gateway_mac_valid = 0;
    dhcp_request_pending = 0;
    memset(&dhcp, 0, sizeof(dhcp));
    tcp_state=tcp_pending_ack=tcp_pending_get=0;
    tcp_next_seq=tcp_remote_seq=tcp_captured=0;
    udp_probe_active = dns_probe_active = 0;
    outw(io + RTL_IMR, 0); /* polling only; don't fire unhandled IRQs */
    outw(io + RTL_ISR, 0xffffu);
    outl(io + RTL_RBSTART, (u32)(uptr)rx_ring);
    /* Accept physical-MAC and broadcast frames only; 8 KiB ring, WRAP.
     * Reserve 16+2048 trailing bytes for DMA packets crossing the ring. */
    outl(io + RTL_RCR, (1u << 7) | (1u << 3) | (1u << 1));
    for (u32 i = 0; i < 4; i++)
        outl(io + RTL_TSAD0 + i * 4u, (u32)(uptr)tx_frames[i]);
    outb(io + RTL_CMD, 0x0c); /* receiver + transmitter */
    nic.present = 1;
    serial("MayOS: RTL8139 native PCI Ethernet ready (polling, IPv4 UDP preview)\n");
    return 1;
}
void may_net_init(void) {
    memset(&nic, 0, sizeof(nic));
    for (u32 bus = 0; bus < 256u; bus++)
        for (u32 slot = 0; slot < 32u; slot++) {
            u32 id0 = pci_read((u8)bus, (u8)slot, 0, 0);
            if ((id0 & 0xffffu) == 0xffffu) continue;
            u32 functions = (pci_read((u8)bus, (u8)slot, 0, 0x0c) &
                             0x00800000u) ? 8u : 1u;
            for (u32 function = 0; function < functions; function++) {
                u32 id = function ? pci_read((u8)bus, (u8)slot,
                                              (u8)function, 0) : id0;
                if ((id & 0xffffu) != RTL_VENDOR ||
                    (id >> 16) != RTL_DEVICE) continue;
                if (rtl_setup((u8)bus, (u8)slot, (u8)function)) return;
            }
        }
    serial("MayOS: no supported RTL8139 PCI Ethernet adapter\n");
}
const MayNetInfo *may_net_info(void) { return &nic; }
const MayDhcpClient *may_net_dhcp_info(void) { return &dhcp; }

const char *may_net_format_ipv4(const u8 ip[4]) {
    static char result[16];
    u32 pos=0;
    for (u32 i=0;i<4;i++) {
        char number_buffer[12];
        number(number_buffer,ip[i]);
        for(u32 j=0;number_buffer[j] && pos<14u;j++)
            result[pos++]=number_buffer[j];
        if(i!=3u)result[pos++]='.';
    }
    result[pos]=0;
    return result;
}
/* QEMU localhost diagnostic: no retransmission, congestion control,
 * production HTTP or encryption. Only sends to already ARP-confirmed gateway. */
static int tcp_transmit(u8 flags,const u8 *data,u16 length) {
    if(!nic.present||!qemu_gateway_mac_valid)return 0;
    u32 index=next_tx;
    u16 io=nic.io_base;
    if(!(inl(io+RTL_TSD0+index*4u)&(1u<<13)))return 0;
    int count=may_tcp_build(tx_frames[index],sizeof(tx_frames[index]),
        nic.mac,qemu_gateway_mac,qemu_local_ip,qemu_gateway_ip,
        40100u,10080u,tcp_next_seq,tcp_remote_seq,
        flags,4096u,data,length,(u16)(tcp_next_seq&0xffffu));
    if(!count)return 0;
    __asm__ volatile("" ::: "memory");
    outl(io+RTL_TSD0+index*4u,(u32)count);
    next_tx=(index+1u)&3u;
    nic.packets_tx++;
    tcp_next_seq+=(u32)length+
                  ((flags&MAY_TCP_SYN)?1u:0u)+((flags&MAY_TCP_FIN)?1u:0u);
    return 1;
}
static void tcp_received(const MayTcpSegment *t) {
    if(t->source_port!=10080u || t->destination_port!=40100u)return;
    for(u32 i=0;i<4;i++)if(t->source_ip[i]!=qemu_gateway_ip[i])return;
    if(t->flags&MAY_TCP_RST) {
        tcp_state=0;
        serial("MayOS: TCP QEMU fixture connection reset\n");
        return;
    }
    if(tcp_state==1) {
        if((t->flags&(MAY_TCP_SYN|MAY_TCP_ACK))==
                       (MAY_TCP_SYN|MAY_TCP_ACK) &&
           t->ack==tcp_next_seq) {
            tcp_remote_seq=t->seq+1u;
            tcp_state=2;
            tcp_pending_ack=1;
            tcp_pending_get=1;
            nic.tcp_fixture_connected=1;
            serial("MayOS: TCP QEMU SYN-ACK validated; handshake established\n");
        }
        return;
    }
    if(tcp_state!=2 || !(t->flags&MAY_TCP_ACK) ||
       t->seq!=tcp_remote_seq || t->ack>tcp_next_seq)return;
    if(t->payload_length) {
        u32 n=t->payload_length;
        tcp_remote_seq+=n;
        tcp_pending_ack=1;
        nic.tcp_fixture_bytes+=n;
        for(u32 i=0;i<n && tcp_captured<sizeof(tcp_capture);i++)
            tcp_capture[tcp_captured++]=t->payload[i];
        static const char prefix[]="HTTP/1.1 200";
        static const char marker[]="MAYOS-OK";
        int good=tcp_captured>=sizeof(prefix)-1u;
        for(u32 i=0;good && i<sizeof(prefix)-1u;i++)
            if(tcp_capture[i]!=(u8)prefix[i])good=0;
        if(good && !nic.tcp_fixture_reply)
            for(u32 i=0;i+sizeof(marker)-1u<=tcp_captured;i++) {
                u32 j=0;
                while(j<sizeof(marker)-1u &&
                      tcp_capture[i+j]==(u8)marker[j])j++;
                if(j==sizeof(marker)-1u) {
                    nic.tcp_fixture_reply=1;
                    serial("MayOS: TCP QEMU HTTP test response validated (plaintext fixture)\n");
                    break;
                }
            }
    }
    if(t->flags&MAY_TCP_FIN) {
        tcp_remote_seq++;
        tcp_pending_ack=1;
    }
}

static int dhcp_transmit(int request) {
    if (!nic.present) return 0;
    u32 index=next_tx;
    u16 io=nic.io_base;
    if (!(inl(io+RTL_TSD0+index*4u) & (1u<<13))) return 0;
    u8 packet[300];
    int payload_bytes=request
        ? may_dhcp_request(&dhcp,packet,sizeof(packet))
        : may_dhcp_discover(&dhcp,packet,sizeof(packet));
    if (!payload_bytes) return 0;
    const u8 zero[4]={0,0,0,0};
    const u8 all_ip[4]={255,255,255,255};
    const u8 broadcast[6]={255,255,255,255,255,255};
    int frame_bytes=may_ipv4_udp_build(tx_frames[index],
                sizeof(tx_frames[index]),nic.mac,broadcast,zero,all_ip,
                68u,67u,packet,(u16)payload_bytes,
                (u16)(0x4d00u+(request?3u:1u)));
    if (!frame_bytes) return 0;
    __asm__ volatile("" ::: "memory");
    outl(io+RTL_TSD0+index*4u,(u32)frame_bytes);
    next_tx=(index+1u)&3u;
    nic.packets_tx++;
    serial(request ? "MayOS: DHCP REQUEST broadcast sent\n"
                   : "MayOS: DHCP DISCOVER broadcast sent\n");
    return 1;
}
int may_net_dhcp_start(void) {
    if (!nic.present) return 0;
    /* Per-session non-secret transaction ID. DHCP messages are not
     * authenticated; only a verified TLS update channel can be trusted. */
    may_dhcp_begin(&dhcp,nic.mac,0x4d41594fu^ticks);
    dhcp_request_pending=0;
    if (dhcp_transmit(0))return 1;
    dhcp.state=MAY_DHCP_IDLE;
    return 0;
}


/* Only accept an ARP response corresponding to the built-in QEMU gateway
 * diagnostic. Don't treat arbitrary LAN packets as successful probes. */
static void receive_frame(u32 offset, u32 frame_len) {
    /* Full frame is copied from the circular RTL8139 DMA ring so the pure
     * parser cannot accidentally read wrapped or stale receive data. */
    if (frame_len >= 42u && frame_len <= sizeof(ipv4_receive) &&
        ring_byte(offset + 12u) == 8u && ring_byte(offset + 13u) == 0u) {
        for (u32 i = 0; i < frame_len; i++)
            ipv4_receive[i] = ring_byte(offset + i);
        MayTcpSegment segment;
        if (may_tcp_parse(ipv4_receive,frame_len,nic.mac,
                          qemu_local_ip,&segment)) {
            tcp_received(&segment);
            return;
        }
        MayUdpDatagram message;
        if (may_ipv4_udp_parse_dhcp(ipv4_receive,frame_len,nic.mac,
                                     qemu_local_ip,&message)) {
            int event=may_dhcp_receive(&dhcp,message.payload,
                                      message.payload_length,
                                      message.source_ip);
            if(event==1) {
                dhcp_request_pending=1;
                serial("MayOS: DHCP OFFER received and validated\n");
            } else if(event==2) {
                serial("MayOS: DHCP ACK accepted; lease acquired ");
                serial(may_net_format_ipv4(dhcp.address));
                serial("\n");
            } else if(event==-1) {
                serial("MayOS: DHCP NAK; lease rejected\n");
            }
            return;
        }
        if (may_ipv4_udp_parse(ipv4_receive, frame_len, nic.mac,
                               qemu_local_ip, &message)) {
            int gateway = 1;
            for (int i = 0; i < 4; i++)
                if (message.source_ip[i] != qemu_gateway_ip[i]) gateway = 0;
            if (gateway && message.source_port == 10053u &&
                udp_probe_active && message.destination_port == 40000u) {
                static const char ack[] = "MAYOS-UDP-ACK";
                if (message.payload_length == sizeof(ack) - 1u) {
                    u32 i;
                    for (i = 0; i < sizeof(ack) - 1u; i++)
                        if (message.payload[i] != (u8)ack[i]) break;
                    if (i == sizeof(ack) - 1u) {
                        nic.ipv4_udp_reply = 1;
                        udp_probe_active = 0;
                        serial("MayOS: RTL8139 IPv4 UDP echo validated from QEMU host\n");
                    }
                }
            } else if (gateway && message.source_port == 10053u &&
                       dns_probe_active && message.destination_port == 40001u) {
                u8 address[4];
                if (may_dns_reply_a(message.payload, message.payload_length,
                                    0x4d41u, address)) {
                    memcpy(nic.dns_fixture_address, address, 4u);
                    nic.dns_fixture_reply = 1;
                    dns_probe_active = 0;
                    serial("MayOS: RTL8139 DNS fixture response validated over IPv4 UDP\n");
                }
            }
        }
        return;
    }
    if (frame_len < 42u) return;
    if (ring_byte(offset + 12u) != 0x08 ||
        ring_byte(offset + 13u) != 0x06) return;
    if (ring_byte(offset + 14u) != 0x00 ||
        ring_byte(offset + 15u) != 0x01 ||
        ring_byte(offset + 16u) != 0x08 ||
        ring_byte(offset + 17u) != 0x00 ||
        ring_byte(offset + 18u) != 6 ||
        ring_byte(offset + 19u) != 4 ||
        ring_byte(offset + 20u) != 0 ||
        ring_byte(offset + 21u) != 2) return;
    const u8 gateway[4] = {10, 0, 2, 2};
    const u8 client[4] = {10, 0, 2, 15};
    for (int i = 0; i < 6; i++) {
        if (ring_byte(offset + (u32)i) != nic.mac[i] ||
            ring_byte(offset + 32u + (u32)i) != nic.mac[i]) return;
    }
    for (int i = 0; i < 4; i++) {
        if (ring_byte(offset + 28u + (u32)i) != gateway[i] ||
            ring_byte(offset + 38u + (u32)i) != client[i]) return;
    }
    int mac_matches = 1;
    for (int i = 0; i < 6; i++)
        if (ring_byte(offset + 6u + (u32)i) !=
            ring_byte(offset + 22u + (u32)i)) mac_matches = 0;
    if (!mac_matches || (ring_byte(offset + 22u) & 1u)) return;
    for (int i = 0; i < 6; i++)
        qemu_gateway_mac[i] = ring_byte(offset + 22u + (u32)i);
    qemu_gateway_mac_valid = 1;
    if (!nic.gateway_arp_reply) {
        nic.gateway_arp_reply = 1;
        serial("MayOS: RTL8139 ARP reply received from QEMU gateway 10.0.2.2\n");
    }
}
void may_net_poll(void) {
    if (!nic.present) return;
    const u16 io = nic.io_base;
    /* ACK masked hardware events in polling mode; no IRQ handler required. */
    u16 events = inw(io + RTL_ISR);
    if (events && events != 0xffffu) outw(io + RTL_ISR, events);
    for (u32 i = 0; i < 16u && !(inb(io + RTL_CMD) & 1u); i++) {
        u16 status = (u16)ring_byte(rx_cursor) |
                     ((u16)ring_byte(rx_cursor + 1u) << 8);
        u32 length = (u32)ring_byte(rx_cursor + 2u) |
                     ((u32)ring_byte(rx_cursor + 3u) << 8);
        if (!(status & 1u) || length < 8u || length > 2048u) {
            nic.rx_errors++;
            serial("MayOS: RTL8139 RX malformed descriptor; stopping poll\n");
            /* Do not advance CAPR using an untrusted DMA length. */
            break;
        }
        receive_frame(rx_cursor + 4u, length - 4u /* hardware CRC */);
        nic.packets_rx++;
        rx_cursor = (rx_cursor + 4u + length + 3u) &
                    ~(u32)3u;
        rx_cursor &= (RTL_RING_BYTES - 1u);
        outw(io + RTL_CAPR, (u16)(rx_cursor - 16u));
    }
    if (dhcp_request_pending && dhcp_transmit(1))
        dhcp_request_pending=0;
    if (tcp_pending_ack && tcp_transmit(MAY_TCP_ACK,0,0))
        tcp_pending_ack=0;
    if (tcp_pending_get) {
        static const char request[]=
            "GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        if(tcp_transmit(MAY_TCP_ACK|MAY_TCP_PSH,(const u8 *)request,
                        sizeof(request)-1u)) {
            tcp_pending_get=0;
            serial("MayOS: TCP QEMU plaintext test request sent\n");
        }
    }
}
int may_net_qemu_arp_probe(void) {
    if (!nic.present) return 0;
    may_net_poll();
    const u16 io = nic.io_base;
    u32 index = next_tx;
    /* The OWN flag must be 1: DMA from the previous frame is finished. */
    if (!(inl(io + RTL_TSD0 + index * 4u) & (1u << 13))) return 0;
    u8 *packet = tx_frames[index];
    memset(packet, 0, 60u);
    for (int i = 0; i < 6; i++) {
        packet[i] = 0xff;         /* Ethernet broadcast */
        packet[6 + i] = nic.mac[i];
        packet[22 + i] = nic.mac[i]; /* ARP sender hardware address */
    }
    packet[12] = 0x08; packet[13] = 0x06; /* Ethernet ARP */
    packet[14] = 0; packet[15] = 1;      /* Ethernet hardware type */
    packet[16] = 0x08; packet[17] = 0;   /* IPv4 protocol */
    packet[18] = 6; packet[19] = 4;
    packet[20] = 0; packet[21] = 1;      /* ARP request */
    packet[28] = 10; packet[29] = 0; packet[30] = 2; packet[31] = 15;
    packet[38] = 10; packet[39] = 0; packet[40] = 2; packet[41] = 2;
    nic.gateway_arp_reply = 0;
    qemu_gateway_mac_valid = 0;
    /* A serial fence ensures the DMA-visible buffer is populated first. */
    __asm__ volatile("" ::: "memory");
    outl(io + RTL_TSD0 + index * 4u, 60u);
    next_tx = (index + 1u) & 3u;
    nic.packets_tx++;
    serial("MayOS: RTL8139 QEMU ARP probe sent to 10.0.2.2\n");
    return 1;
}


/* Both probes intentionally use a fixed, non-routable QEMU host fixture.
 * They are not a public resolver, Internet interface, nor an update API. */
static int send_ipv4_probe(int dns) {
    if (!nic.present || !qemu_gateway_mac_valid) return 0;
    may_net_poll();
    u32 index = next_tx;
    const u16 io = nic.io_base;
    if (!(inl(io + RTL_TSD0 + index * 4u) & (1u << 13))) return 0;
    static const u8 echo_request[] = "MAYOS-UDP-PROBE";
    u8 dns_request[280];
    const u8 *data = echo_request;
    u16 data_size = (u16)(sizeof(echo_request) - 1u);
    if (dns) {
        int n = may_dns_query_a(dns_request, sizeof(dns_request),
                                "example.test", 0x4d41u);
        if (!n) return 0;
        data = dns_request;
        data_size = (u16)n;
    }
    int frame_size = may_ipv4_udp_build(
        tx_frames[index], sizeof(tx_frames[index]),
        nic.mac, qemu_gateway_mac, qemu_local_ip, qemu_gateway_ip,
        dns ? 40001u : 40000u, 10053u,
        data, data_size, dns ? 0x4d41u : 0x4d40u);
    if (!frame_size) return 0;
    if (dns) {
        nic.dns_fixture_reply = 0;
        dns_probe_active = 1;
    } else {
        nic.ipv4_udp_reply = 0;
        udp_probe_active = 1;
    }
    __asm__ volatile("" ::: "memory");
    outl(io + RTL_TSD0 + index * 4u, (u32)frame_size);
    next_tx = (index + 1u) & 3u;
    nic.packets_tx++;
    serial(dns ? "MayOS: RTL8139 DNS fixture request sent over IPv4 UDP\n"
               : "MayOS: RTL8139 IPv4 UDP echo probe sent to QEMU host\n");
    return 1;
}
int may_net_qemu_udp_probe(void) {
    return send_ipv4_probe(0);
}
int may_net_qemu_dns_fixture(void) {
    return send_ipv4_probe(1);
}

int may_net_qemu_tcp_http_probe(void) {
    if(!nic.present||!qemu_gateway_mac_valid)return 0;
    may_net_poll();
    tcp_next_seq=0x4d415900u^ticks;
    tcp_remote_seq=0;
    tcp_captured=0;
    tcp_state=1;
    tcp_pending_ack=tcp_pending_get=0;
    nic.tcp_fixture_connected=0;
    nic.tcp_fixture_reply=0;
    nic.tcp_fixture_bytes=0;
    if(!tcp_transmit(MAY_TCP_SYN,0,0)) {
        tcp_state=0;
        return 0;
    }
    serial("MayOS: TCP QEMU SYN sent to localhost fixture\n");
    return 1;
}

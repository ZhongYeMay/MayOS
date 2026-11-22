#include "net.h"
#include "io.h"
#include "pci.h"
#include "e1000.h"
#include "net_ipv4.h"
#include "net_dhcp.h"
#include "net_tcp.h"
#include "net_tcp_retry.h"

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
static u8 dns_query[280], dns_next_hop[4], dns_next_mac[6];
static u16 dns_query_length, dns_source_port, dns_sequence;
static u32 dns_started_tick, dns_arp_tick;
static unsigned dns_arp_attempts;
enum { DNS_IDLE, DNS_WAIT_ARP, DNS_SEND, DNS_WAIT_REPLY };
static u16 dns_native_txid;
static u32 dns_native_sent_tick;
static int dns_native_pending;
static int qemu_gateway_mac_valid, udp_probe_active, dns_probe_active;
static MayDhcpClient dhcp;
static u32 dhcp_next_tick, dhcp_last_tick, dhcp_round;
static u64 dhcp_lease_ticks;
static unsigned dhcp_attempts;
#define DHCP_TRIES 4
#define DHCP_ROUND_DELAY 3000u
static u32 tcp_next_seq,tcp_remote_seq,tcp_captured,tcp_probe_serial;
static u16 tcp_local_port;
static int tcp_state,tcp_pending_ack,tcp_pending_get,tcp_pending_fin;
static int tcp_drop_next_syn;
static u32 tcp_response_tick;
static MayTcpRetry tcp_retry;
static u8 tcp_retry_payload[128];
static u8 tcp_capture[256];
static const u8 qemu_local_ip[4] = {10, 0, 2, 15};
static const u8 qemu_gateway_ip[4] = {10, 0, 2, 2};

static u8 ring_byte(u32 address) {
    return rx_ring[address & (RTL_RING_BYTES - 1u)];
}
static int address_32bit(const void *pointer) {
    uptr value = (uptr)pointer;
    return value != 0 && value <= 0xffffffffu;
}
static int rtl_setup(const PciDevice *dev) {
    u8 bus = dev->bus, slot = dev->slot, func = dev->function;
    u32 bar = dev->bar[0];
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
    u16 old = pci_config_read16(bus, slot, func, 0x04);
    pci_config_write16(bus, slot, func, 0x04, old | 0x5u); /* PCI I/O + bus master */
    if ((pci_config_read16(bus, slot, func, 0x04) & 0x5u) != 0x5u) {
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
    dns_native_pending=0;
    memset(&dhcp, 0, sizeof(dhcp));
    tcp_state=tcp_pending_ack=tcp_pending_get=tcp_pending_fin=0;
    tcp_drop_next_syn=0;
    memset(&tcp_retry,0,sizeof(tcp_retry));
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
    for (int i = 0; i < pci_device_count(); i++) {
        const PciDevice *dev = pci_device_at(i);
        if (dev->vendor_id == RTL_VENDOR && dev->device_id == RTL_DEVICE &&
            rtl_setup(dev)) return;
    }
    if (e1000_init()) {
        memcpy(nic.mac, e1000_mac_address(), sizeof(nic.mac));
        nic.present = 1;
        return;
    }
    serial("MayOS: no supported PCI Ethernet adapter\n");
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
/* Single outstanding segment, bounded retries, and explicit close.
 * Only for a QEMU localhost fixture; not congestion-controlled TCP. */
static int tx_available(void) {
    return !nic.io_base ||
           (inl(nic.io_base + RTL_TSD0 + next_tx * 4u) & (1u << 13));
}

static int send_frame(u32 length) {
    if (nic.io_base) {
        __asm__ volatile("mfence" ::: "memory");
        outl(nic.io_base + RTL_TSD0 + next_tx * 4u, length);
        next_tx = (next_tx + 1u) & 3u;
    } else if (!e1000_send(tx_frames[next_tx], length)) {
        return 0;
    }
    nic.packets_tx++;
    nic.bytes_tx += length;
    return 1;
}

static int tcp_transmit(u8 flags,const u8 *data,u16 length) {
    if(!nic.present||!qemu_gateway_mac_valid||
       length>sizeof(tcp_retry_payload))return 0;
    u32 index=next_tx;
    if (!tx_available()) return 0;
    int count=may_tcp_build(tx_frames[index],sizeof(tx_frames[index]),
        nic.mac,qemu_gateway_mac,qemu_local_ip,qemu_gateway_ip,
        tcp_local_port,10080u,tcp_next_seq,tcp_remote_seq,
        flags,4096u,data,length,(u16)(tcp_next_seq&0xffffu));
    if(!count)return 0;
    if(flags&(MAY_TCP_SYN|MAY_TCP_PSH|MAY_TCP_FIN)) {
        if(tcp_retry.active)return 0; /* one inflight segment */
        may_tcp_retry_start(&tcp_retry,tcp_next_seq,length,flags,ticks);
        if(length)memcpy(tcp_retry_payload,data,length);
    }
    if(tcp_drop_next_syn && (flags&MAY_TCP_SYN)) {
        tcp_drop_next_syn=0;
        serial("MayOS: TCP diagnostic intentionally dropped first SYN\n");
    } else {
        if (!send_frame((u32)count)) {
            if (flags & (MAY_TCP_SYN | MAY_TCP_PSH | MAY_TCP_FIN))
                may_tcp_retry_cancel(&tcp_retry);
            return 0;
        }
    }
    tcp_next_seq+=(u32)length+
                  ((flags&MAY_TCP_SYN)?1u:0u)+((flags&MAY_TCP_FIN)?1u:0u);
    if(flags&MAY_TCP_PSH)tcp_response_tick=ticks;
    return 1;
}
static void tcp_retransmit_poll(void) {
    if(!tcp_state||!tcp_retry.active)return;
    if(may_tcp_retry_exhausted(&tcp_retry,ticks)) {
        may_tcp_retry_cancel(&tcp_retry);
        tcp_state=0;
        nic.tcp_fixture_timeout=1;
        serial("MayOS: TCP diagnostic retry limit reached; connection aborted\n");
        return;
    }
    if(!may_tcp_retry_due(&tcp_retry,ticks))return;
    u32 index=next_tx;
    if (!tx_available()) return;
    int count=may_tcp_build(tx_frames[index],sizeof(tx_frames[index]),
        nic.mac,qemu_gateway_mac,qemu_local_ip,qemu_gateway_ip,
        tcp_local_port,10080u,tcp_retry.first_seq,tcp_remote_seq,
        tcp_retry.flags,4096u,tcp_retry_payload,tcp_retry.payload_length,
        (u16)(tcp_retry.first_seq&0xffffu));
    if(!count)return;
    if (!send_frame((u32)count)) return;
    may_tcp_retry_resent(&tcp_retry,ticks);
    nic.tcp_fixture_retries++;
    serial("MayOS: TCP diagnostic segment retransmitted\n");
}
static void tcp_received(const MayTcpSegment *t) {
    if(t->source_port!=10080u || t->destination_port!=tcp_local_port)return;
    for(u32 i=0;i<4;i++)if(t->source_ip[i]!=qemu_gateway_ip[i])return;
    if(t->flags&MAY_TCP_RST) {
        may_tcp_retry_cancel(&tcp_retry);
        tcp_state=0;
        serial("MayOS: TCP QEMU fixture connection reset\n");
        return;
    }
    if(tcp_state==1) {
        if((t->flags&(MAY_TCP_SYN|MAY_TCP_ACK))==
                       (MAY_TCP_SYN|MAY_TCP_ACK) &&
           t->ack==tcp_next_seq &&
           may_tcp_retry_ack(&tcp_retry,t->ack)) {
            tcp_remote_seq=t->seq+1u;
            tcp_state=2;
            tcp_pending_ack=1;
            tcp_pending_get=1;
            nic.tcp_fixture_connected=1;
            serial("MayOS: TCP QEMU SYN-ACK validated; handshake established\n");
        }
        return;
    }
    if((tcp_state!=2 && tcp_state!=3) ||
       !(t->flags&MAY_TCP_ACK) ||
       t->seq!=tcp_remote_seq || t->ack>tcp_next_seq)return;
    (void)may_tcp_retry_ack(&tcp_retry,t->ack);
    if(tcp_state==3 && t->ack==tcp_next_seq) {
        tcp_state=4;
        nic.tcp_fixture_closed=1;
        serial("MayOS: TCP diagnostic FIN acknowledged; clean close\n");
        return;
    }
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
        tcp_pending_ack=0;
        tcp_pending_fin=1;
    }
}

static int dhcp_transmit(int request) {
    if (!nic.present) return 0;
    u32 index=next_tx;
    if (!tx_available()) return 0;
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
    if (!send_frame((u32)frame_bytes)) return 0;
    serial(request ? "MayOS: DHCP REQUEST broadcast sent\n"
                   : "MayOS: DHCP DISCOVER broadcast sent\n");
    return 1;
}
static void dhcp_begin(u32 now) {
    /* DHCP transaction identity is not an authentication credential. */
    may_dhcp_begin(&dhcp, nic.mac, 0x4d41594fu ^ now ^ ++dhcp_round);
    dhcp_attempts = 0;
    dhcp_next_tick = now;
    dhcp_lease_ticks = 0;
}

int may_net_dhcp_start(void) {
    if (!nic.present) return 0;
    if (dhcp.state == MAY_DHCP_IDLE || dhcp.state == MAY_DHCP_NAK)
        dhcp_begin(ticks);
    return 1;
}

static void dhcp_poll(void) {
    u32 now = ticks;
    u32 elapsed = now - dhcp_last_tick;
    dhcp_last_tick = now;
    if (dhcp.state == MAY_DHCP_BOUND) {
        if (elapsed < dhcp_lease_ticks) {
            dhcp_lease_ticks -= elapsed;
            return;
        }
        dns_native_pending = 0;
        serial("MayOS: DHCP lease expired; starting discovery again\n");
        dhcp_begin(now);
    }
    if ((i32)(now - dhcp_next_tick) < 0) return;
    if (dhcp.state == MAY_DHCP_IDLE || dhcp.state == MAY_DHCP_NAK)
        dhcp_begin(now);
    if (dhcp_attempts == DHCP_TRIES) {
        dhcp.state = MAY_DHCP_IDLE;
        dhcp_next_tick = now + DHCP_ROUND_DELAY;
        serial("MayOS: DHCP response timeout; retry scheduled\n");
        return;
    }
    if (dhcp_transmit(dhcp.state != MAY_DHCP_DISCOVER)) {
        dhcp_next_tick = now + (100u << dhcp_attempts);
        dhcp_attempts++;
    } else dhcp_next_tick = now + 10u;
}

static int net_equal(const u8 *a, const u8 *b, unsigned length) {
    for (unsigned i = 0; i < length; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int arp_send(const u8 source[4], const u8 destination[4],
                    const u8 *destination_mac) {
    if (!tx_available()) return 0;
    u8 *packet = tx_frames[next_tx];
    memset(packet, 0, 60u);
    if (destination_mac) {
        memcpy(packet, destination_mac, 6);
        memcpy(packet + 32, destination_mac, 6);
    } else memset(packet, 0xff, 6);
    memcpy(packet + 6, nic.mac, 6);
    memcpy(packet + 22, nic.mac, 6);
    packet[12] = 8; packet[13] = 6;
    packet[15] = 1; packet[16] = 8;
    packet[18] = 6; packet[19] = 4;
    packet[21] = destination_mac ? 2 : 1;
    memcpy(packet + 28, source, 4);
    memcpy(packet + 38, destination, 4);
    return send_frame(60u);
}

static void arp_receive(const u8 *frame, u32 length) {
    static const u8 header[] = {8, 6, 0, 1, 8, 0, 6, 4, 0};
    static const u8 broadcast[] = {255, 255, 255, 255, 255, 255};
    static const u8 null_mac[6];
    if (length < 42 || !net_equal(frame + 12, header, sizeof(header)) ||
        !net_equal(frame + 6, frame + 22, 6) || (frame[22] & 1u) ||
        net_equal(frame + 22, null_mac, 6)) return;
    if (frame[21] == 1 && dhcp.state == MAY_DHCP_BOUND &&
        net_equal(frame + 38, dhcp.address, 4) &&
        (net_equal(frame, nic.mac, 6) || net_equal(frame, broadcast, 6))) {
        arp_send(dhcp.address, frame + 28, frame + 22);
        return;
    }
    if (frame[21] != 2 || !net_equal(frame, nic.mac, 6) ||
        !net_equal(frame + 32, nic.mac, 6)) return;
    if (dns_native_pending == DNS_WAIT_ARP && dns_arp_attempts &&
        net_equal(frame + 28, dns_next_hop, 4) && net_equal(frame + 38, dhcp.address, 4)) {
        memcpy(dns_next_mac, frame + 22, 6);
        dns_native_pending = DNS_SEND;
    }
    if (net_equal(frame + 28, qemu_gateway_ip, 4) &&
        net_equal(frame + 38, qemu_local_ip, 4)) {
        memcpy(qemu_gateway_mac, frame + 22, 6);
        qemu_gateway_mac_valid = 1;
        if (!nic.gateway_arp_reply) {
            nic.gateway_arp_reply = 1;
            serial("MayOS: Ethernet ARP reply received from QEMU gateway 10.0.2.2\n");
        }
    }
}

static void dns_receive(const MayUdpDatagram *message) {
    if (dns_native_pending != DNS_WAIT_REPLY || message->source_port != 53u ||
        message->destination_port != dns_source_port ||
        message->payload_length < dns_query_length ||
        !net_equal(message->source_ip, dhcp.dns, 4) ||
        !net_equal(message->payload + 12, dns_query + 12, dns_query_length - 12u)) return;
    u8 address[4];
    if (may_dns_reply_a(message->payload, message->payload_length, dns_native_txid, address)) {
        memcpy(nic.dns_native_address, address, 4);
        nic.dns_native_reply = 1;
        dns_native_pending = DNS_IDLE;
        serial("MayOS: DHCP DNS server returned a matched A record\n");
    }
}

static void dns_poll(void) {
    if (dns_native_pending == DNS_IDLE) return;
    if (dhcp.state != MAY_DHCP_BOUND || (u32)(ticks - dns_started_tick) >= 500u ||
        (dns_native_pending == DNS_WAIT_REPLY && (u32)(ticks - dns_native_sent_tick) >= 350u)) {
        dns_native_pending = DNS_IDLE;
        nic.dns_native_timeout = 1;
        serial("MayOS: DHCP DNS lookup timed out\n");
        return;
    }
    if (dns_native_pending == DNS_WAIT_ARP) {
        if (dns_arp_attempts < 3 && (!dns_arp_attempts || (u32)(ticks - dns_arp_tick) >= 75u) &&
            arp_send(dhcp.address, dns_next_hop, 0)) {
            dns_arp_attempts++;
            dns_arp_tick = ticks;
        }
        return;
    }
    if (dns_native_pending != DNS_SEND || !tx_available()) return;
    int count = may_ipv4_udp_build(tx_frames[next_tx], sizeof(tx_frames[next_tx]), nic.mac,
        dns_next_mac, dhcp.address, dhcp.dns, dns_source_port, 53u,
        dns_query, dns_query_length, dns_native_txid);
    if (count && send_frame((u32)count)) {
        dns_native_pending = DNS_WAIT_REPLY;
        dns_native_sent_tick = ticks;
        serial("MayOS: DHCP DNS A query sent to leased DNS server\n");
    }
}

static void receive_frame(const u8 *frame, u32 frame_len) {
    if (frame_len >= 42u && frame[12] == 8u && frame[13] == 0u) {
        MayTcpSegment segment;
        if (may_tcp_parse(frame,frame_len,nic.mac,
                          qemu_local_ip,&segment)) {
            tcp_received(&segment);
            return;
        }
        MayUdpDatagram message;
        if (may_ipv4_udp_parse_dhcp(frame,frame_len,nic.mac,
                                     qemu_local_ip,&message)) {
            int event=may_dhcp_receive(&dhcp,message.payload,
                                      message.payload_length,
                                      message.source_ip);
            if(event==1) {
                dhcp_attempts = 0;
                dhcp_next_tick = ticks;
                serial("MayOS: DHCP OFFER received and validated\n");
            } else if(event==2) {
                dhcp_last_tick = ticks;
                dhcp_lease_ticks = (u64)dhcp.lease_seconds * 100u;
                serial("MayOS: DHCP ACK accepted; lease acquired ");
                serial(may_net_format_ipv4(dhcp.address));
                serial("\n");
            } else if(event==-1) {
                dhcp_next_tick = ticks + DHCP_ROUND_DELAY;
                serial("MayOS: DHCP NAK; lease rejected\n");
            }
            return;
        }
        if (dhcp.state == MAY_DHCP_BOUND &&
            may_ipv4_udp_parse(frame, frame_len, nic.mac, dhcp.address, &message))
            dns_receive(&message);
        if (may_ipv4_udp_parse(frame, frame_len, nic.mac,
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
                        serial("MayOS: Ethernet IPv4 UDP echo validated from QEMU host\n");
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
                    serial("MayOS: Ethernet DNS fixture response validated over IPv4 UDP\n");
                }
            }
        }
        return;
    }
    arp_receive(frame, frame_len);
}
void may_net_poll(void) {
    if (!nic.present) return;
    if (!nic.io_base) {
        for (int i = 0; i < 16; i++) {
            int length = e1000_receive(ipv4_receive, sizeof(ipv4_receive));
            if (length <= 0) break;
            receive_frame(ipv4_receive, (u32)length);
            nic.packets_rx++;
            nic.bytes_rx += (u32)length;
        }
    } else {
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
            u32 frame_length = length - 4u; /* Exclude the hardware CRC. */
            if (frame_length <= sizeof(ipv4_receive)) {
                for (u32 byte = 0; byte < frame_length; byte++)
                    ipv4_receive[byte] = ring_byte(rx_cursor + 4u + byte);
                receive_frame(ipv4_receive, frame_length);
            }
            nic.packets_rx++;
            nic.bytes_rx += frame_length;
            rx_cursor = (rx_cursor + 4u + length + 3u) &
                        ~(u32)3u;
            rx_cursor &= (RTL_RING_BYTES - 1u);
            outw(io + RTL_CAPR, (u16)(rx_cursor - 16u));
        }
    }
    dhcp_poll();
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
    if(tcp_pending_fin && !tcp_retry.active &&
       tcp_transmit(MAY_TCP_FIN|MAY_TCP_ACK,0,0)) {
        tcp_pending_fin=0;
        tcp_state=3;
        serial("MayOS: TCP diagnostic FIN sent; closing connection\n");
    }
    tcp_retransmit_poll();
    dns_poll();
    if(tcp_state==2 && !nic.tcp_fixture_reply &&
       !tcp_retry.active && !tcp_pending_get &&
       (u32)(ticks-tcp_response_tick)>=800u) {
        tcp_state=0;
        nic.tcp_fixture_timeout=1;
        serial("MayOS: TCP diagnostic response timed out\n");
    }
}
int may_net_qemu_arp_probe(void) {
    if (!nic.present) return 0;
    may_net_poll();
    nic.gateway_arp_reply = 0;
    qemu_gateway_mac_valid = 0;
    if (!arp_send(qemu_local_ip, qemu_gateway_ip, 0)) return 0;
    serial("MayOS: Ethernet QEMU ARP probe sent to 10.0.2.2\n");
    return 1;
}


/* Both probes intentionally use a fixed, non-routable QEMU host fixture.
 * They are not a public resolver, Internet interface, nor an update API. */
static int send_ipv4_probe(int dns) {
    if (!nic.present || !qemu_gateway_mac_valid) return 0;
    may_net_poll();
    u32 index = next_tx;
    if (!tx_available()) return 0;
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
    if (!frame_size || !send_frame((u32)frame_size)) return 0;
    if (dns) {
        nic.dns_fixture_reply = 0;
        dns_probe_active = 1;
    } else {
        nic.ipv4_udp_reply = 0;
        udp_probe_active = 1;
    }
    serial(dns ? "MayOS: Ethernet DNS fixture request sent over IPv4 UDP\n"
               : "MayOS: Ethernet IPv4 UDP echo probe sent to QEMU host\n");
    return 1;
}
int may_net_qemu_udp_probe(void) {
    return send_ipv4_probe(0);
}
int may_net_qemu_dns_fixture(void) {
    return send_ipv4_probe(1);
}

static int start_tcp_probe(int lose_first_syn) {
    if(!nic.present||!qemu_gateway_mac_valid)return 0;
    may_net_poll();
    if(tcp_state && tcp_state!=4)return 0;
    /* Each connection needs a fresh four-tuple. Reusing the just-closed
     * source port can collide with QEMU SLIRP's TCP TIME_WAIT state. */
    tcp_local_port=(u16)(40100u+(tcp_probe_serial++%1000u));
    tcp_next_seq=0x4d415900u^ticks;
    tcp_remote_seq=0;
    tcp_captured=0;
    tcp_state=1;
    tcp_pending_ack=tcp_pending_get=tcp_pending_fin=0;
    tcp_drop_next_syn=lose_first_syn;
    may_tcp_retry_cancel(&tcp_retry);
    nic.tcp_fixture_connected=0;
    nic.tcp_fixture_closed=0;
    nic.tcp_fixture_timeout=0;
    nic.tcp_fixture_retries=0;
    nic.tcp_fixture_reply=0;
    nic.tcp_fixture_bytes=0;
    if(!tcp_transmit(MAY_TCP_SYN,0,0)) {
        tcp_state=0;
        return 0;
    }
    serial("MayOS: TCP QEMU SYN sent to localhost fixture\n");
    return 1;
}
int may_net_qemu_tcp_http_probe(void) { return start_tcp_probe(0); }
int may_net_qemu_tcp_loss_probe(void) { return start_tcp_probe(1); }


int may_net_dns_lookup(const char *hostname) {
    if (!nic.present || !hostname || dhcp.state != MAY_DHCP_BOUND ||
        dns_native_pending || !dhcp.dns[0] || strlen(hostname) > 72u) return 0;
    if (!may_ipv4_next_hop(dhcp.address, dhcp.subnet, dhcp.router, dhcp.dns, dns_next_hop)) return 0;
    u16 sequence = ++dns_sequence;
    u16 id = (u16)(0x4d42u ^ ticks ^ sequence);
    if (!id) id = 1;
    int count = may_dns_query_a(dns_query, sizeof(dns_query), hostname, id);
    if (count <= 12) return 0;
    dns_query_length = (u16)count;
    dns_native_txid = id;
    dns_source_port = (u16)(45000u + sequence % 20000u);
    dns_started_tick = ticks;
    dns_arp_attempts = 0;
    nic.dns_native_reply = nic.dns_native_timeout = 0;
    dns_native_pending = DNS_WAIT_ARP;
    return 1;
}

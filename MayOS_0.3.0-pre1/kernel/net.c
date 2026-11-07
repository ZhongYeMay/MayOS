/* MayKernel RTL8139 + tiny IPv4 stack for QEMU/PC.
 * Polling Ethernet, ARP, DHCP, ICMP echo, UDP DNS A query; no TCP/TLS.
 * Freestanding, no libc, no dynamic memory. This is not a production stack.
 */
#include "core.h"
#include "io.h"

#define RX_SIZE 65536u
#define ETH_IP 0x0800
#define ETH_ARP 0x0806
#define IP_UDP 17
#define IP_ICMP 1

static u16 nic_io;
static u8 mac[6], gateway_mac[6];
static u8 rx_ring[RX_SIZE + 2048u] __attribute__((aligned(256)));
static u8 tx_frames[4][1600] __attribute__((aligned(4)));
static u8 frame[1600];
static u16 rx_at, tx_slot;
static u32 ip_addr, subnet_mask, gateway, dns_server, dhcp_server;
static u32 offered_ip, last_dhcp, last_arp, identifier;
static int link_ready, lease_ready, gateway_ready, dhcp_phase;
static int ping_replies, dns_replies;
static u32 http_ip, http_seq, http_ack, http_started;
static u16 http_port, http_source_port;
static int http_stage, http_used;
static char http_path[240], http_host[64], http_response[8192];



static u16 be16(const u8 *p) { return (u16)((u16)p[0] << 8 | p[1]); }
static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static void wr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void wr32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
static u16 checksum(const u8 *data, u32 length) {
    u32 sum = 0;
    for (u32 i = 0; i + 1 < length; i += 2)
        sum += be16(data + i);
    if (length & 1)
        sum += (u32)data[length - 1] << 8;
    while (sum >> 16)
        sum = (sum & 65535u) + (sum >> 16);
    return (u16)~sum;
}
static int equal6(const u8 *a, const u8 *b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static void fill(u8 *p, u8 v, u32 n) { for (u32 i = 0; i < n; i++) p[i] = v; }
static u16 read_ring16(u32 at) {
    return (u16)(rx_ring[at & 65535u] | (u16)rx_ring[(at + 1) & 65535u] << 8);
}
static void tx(const u8 *data, u16 length) {
    if (!link_ready || length < 14 || length > 1500) return;
    u16 slot = tx_slot++ & 3u;
    u32 tsd = inl((u16)(nic_io + 0x10 + slot * 4));
    if ((tsd & (1u << 13)) == 0 && (tsd & (1u << 15)) == 0 &&
        (tsd & (1u << 31)) != 0) return;
    memcpy(tx_frames[slot], data, length);
    if (length < 60) {
        memset(tx_frames[slot] + length, 0, 60 - length);
        length = 60;
    }
    outl((u16)(nic_io + 0x20 + slot * 4), (u32)tx_frames[slot]);
    outl((u16)(nic_io + 0x10 + slot * 4), length);
}
static void ethernet(u8 *p, const u8 *destination, u16 type) {
    memcpy(p, destination, 6);
    memcpy(p + 6, mac, 6);
    wr16(p + 12, type);
}
static void arp_request(u32 target) {
    u8 p[60];
    fill(p, 0, sizeof(p));
    fill(p, 255, 6);
    ethernet(p, p, ETH_ARP);
    wr16(p + 14, 1); wr16(p + 16, ETH_IP);
    p[18] = 6; p[19] = 4; wr16(p + 20, 1);
    memcpy(p + 22, mac, 6); wr32(p + 28, ip_addr);
    wr32(p + 38, target);
    tx(p, sizeof(p));
}
static void arp_receive(const u8 *p, u16 length) {
    if (length < 42 || be16(p + 14) != 1 || be16(p + 16) != ETH_IP ||
        p[18] != 6 || p[19] != 4) return;
    u16 op = be16(p + 20);
    u32 sender = be32(p + 28), target = be32(p + 38);
    if (sender == gateway && gateway) {
        memcpy(gateway_mac, p + 22, 6);
        gateway_ready = 1;
        serial("MayNet: gateway ARP resolved\n");
    }
    if (op != 1 || !lease_ready || target != ip_addr) return;
    u8 reply[60];
    fill(reply, 0, sizeof(reply));
    ethernet(reply, p + 22, ETH_ARP);
    wr16(reply + 14, 1); wr16(reply + 16, ETH_IP);
    reply[18] = 6; reply[19] = 4; wr16(reply + 20, 2);
    memcpy(reply + 22, mac, 6); wr32(reply + 28, ip_addr);
    memcpy(reply + 32, p + 22, 6); wr32(reply + 38, sender);
    tx(reply, sizeof(reply));
}
static int ipv4(u8 *p, const u8 *destination_mac, u32 source, u32 destination,
                u8 protocol, const u8 *payload, u16 length) {
    if ((u32)length + 34u > 1500u) return 0;
    ethernet(p, destination_mac, ETH_IP);
    u8 *h = p + 14;
    memset(h, 0, 20);
    h[0] = 0x45; wr16(h + 2, (u16)(length + 20));
    wr16(h + 4, (u16)++identifier);
    h[8] = 64; h[9] = protocol;
    wr32(h + 12, source); wr32(h + 16, destination);
    wr16(h + 10, checksum(h, 20));
    memcpy(h + 20, payload, length);
    return length + 34;
}
static void udp_send(const u8 *destination_mac, u32 source, u32 destination,
                     u16 from, u16 to, const u8 *payload, u16 length) {
    u8 packet[1490];
    if (length > sizeof(packet) - 8) return;
    wr16(packet, from); wr16(packet + 2, to);
    wr16(packet + 4, (u16)(length + 8));
    packet[6] = packet[7] = 0;
    memcpy(packet + 8, payload, length);
    int count = ipv4(frame, destination_mac, source, destination,
                     IP_UDP, packet, length + 8);
    if (count) tx(frame, (u16)count);
}
static void dhcp_send(int request) {
    u8 d[320];
    u8 broadcast[6];
    memset(d, 0, sizeof(d)); fill(broadcast, 255, 6);
    d[0] = 1; d[1] = 1; d[2] = 6;
    wr32(d + 4, 0x4d415930);
    d[10] = 0x80;
    memcpy(d + 28, mac, 6);
    wr32(d + 236, 0x63825363);
    int n = 240;
    d[n++] = 53; d[n++] = 1; d[n++] = request ? 3 : 1;
    if (request) {
        d[n++] = 50; d[n++] = 4; wr32(d + n, offered_ip); n += 4;
        d[n++] = 54; d[n++] = 4; wr32(d + n, dhcp_server); n += 4;
    }
    d[n++] = 55; d[n++] = 3;
    d[n++] = 1; d[n++] = 3; d[n++] = 6;
    d[n++] = 12; d[n++] = 5;
    memcpy(d + n, "MayOS", 5); n += 5;
    d[n++] = 255;
    udp_send(broadcast, 0, 0xffffffffu, 68, 67, d, (u16)n);
    last_dhcp = ticks;
    serial(request ? "MayNet: DHCP REQUEST\n" : "MayNet: DHCP DISCOVER\n");
}
static void dhcp_receive(const u8 *p, u16 length) {
    if (length < 240 || p[0] != 2 || p[1] != 1 || p[2] != 6 ||
        be32(p + 4) != 0x4d415930 || be32(p + 236) != 0x63825363 ||
        !equal6(p + 28, mac)) return;
    u32 address = be32(p + 16);
    u32 mask = 0xffffff00u, router = 0, dns = 0, server = 0;
    u8 type = 0;
    for (u32 i = 240; i < length;) {
        u8 code = p[i++];
        if (code == 255) break;
        if (code == 0) continue;
        if (i >= length) break;
        u8 size = p[i++];
        if ((u32)size > length - i) break;
        if (code == 53 && size >= 1) type = p[i];
        if (code == 1 && size >= 4) mask = be32(p + i);
        if (code == 3 && size >= 4) router = be32(p + i);
        if (code == 6 && size >= 4) dns = be32(p + i);
        if (code == 54 && size >= 4) server = be32(p + i);
        i += size;
    }
    if (type == 2 && dhcp_phase == 1) {
        offered_ip = address;
        dhcp_server = server ? server : be32(p + 20);
        dhcp_phase = 2;
        dhcp_send(1);
    } else if (type == 5 && dhcp_phase == 2) {
        ip_addr = address; subnet_mask = mask;
        gateway = router; dns_server = dns;
        lease_ready = 1; dhcp_phase = 3;
        serial("MayNet: DHCP ready\n");
        if (gateway) arp_request(gateway);
    } else if (type == 6) {
        dhcp_phase = 1;
        lease_ready = 0;
        dhcp_send(0);
    }
}
static void icmp_receive(const u8 *p, u16 length) {
    if (length >= 8 && p[0] == 0 && be16(p + 4) == 0x4d59) {
        ping_replies++;
        serial("MayNet: ICMP echo reply\n");
    }
}
static void dns_receive(const u8 *p, u16 length) {
    if (length < 12 || be16(p) != 0x4d59 || !(p[2] & 0x80)) return;
    u32 at = 12;
    while (at < length && p[at]) {
        if (p[at] > 63 || at + p[at] + 1 >= length) return;
        at += 1 + p[at];
    }
    if (at + 5 >= length) return;
    at += 5;
    u16 count = be16(p + 6);
    for (u16 i = 0; i < count && at + 12 <= length; i++) {
        if ((p[at] & 0xc0) == 0xc0) at += 2;
        else {
            while (at < length && p[at]) {
                if (p[at] > 63 || at + p[at] + 1 >= length) return;
                at += 1 + p[at];
            }
            at++;
        }
        if (at + 10 > length) return;
        u16 type = be16(p + at), klass = be16(p + at + 2);
        u16 size = be16(p + at + 8);
        at += 10;
        if (at + size > length) return;
        if (type == 1 && klass == 1 && size == 4) {
            dns_replies++;
            serial("MayNet: DNS A reply\n");
            return;
        }
        at += size;
    }
}
static void udp_receive(const u8 *p, u16 length) {
    if (length < 8) return;
    u16 from = be16(p), to = be16(p + 2), size = be16(p + 4);
    if (size < 8 || size > length) return;
    if (to == 68 && from == 67) dhcp_receive(p + 8, size - 8);
    if (to == 53000 && from == 53) dns_receive(p + 8, size - 8);
}
/* Single outstanding HTTP/1.0 connection over IPv4 TCP. */
static void tcp_send(u8 flags, const u8 *data, u16 length) {
    u8 segment[768], pseudo[12 + sizeof(segment)];
    if (length > sizeof(segment) - 20 || !gateway_ready) return;
    memset(segment, 0, 20);
    wr16(segment, http_source_port); wr16(segment + 2, http_port);
    wr32(segment + 4, http_seq); wr32(segment + 8, http_ack);
    segment[12] = 0x50; segment[13] = flags;
    wr16(segment + 14, 4096);
    if (length) memcpy(segment + 20, data, length);
    wr32(pseudo, ip_addr); wr32(pseudo + 4, http_ip);
    pseudo[8] = 0; pseudo[9] = 6;
    wr16(pseudo + 10, (u16)(20 + length));
    memcpy(pseudo + 12, segment, (u32)20 + length);
    wr16(segment + 16, checksum(pseudo, (u32)32 + length));
    int count = ipv4(frame, gateway_mac, ip_addr, http_ip, 6, segment, (u16)(20 + length));
    if (count) tx(frame, (u16)count);
}
static void tcp_receive(const u8 *p, u16 length, u32 source) {
    if (!http_stage || http_stage == 3 || source != http_ip ||
        length < 20 || be16(p) != http_port || be16(p + 2) != http_source_port)
        return;
    u32 seq = be32(p + 4);
    u32 hlen = (u32)(p[12] >> 4) * 4;
    if (hlen < 20 || hlen > length) return;
    u8 flags = p[13];
    if (http_stage == 1 && (flags & 0x12) == 0x12) {
        http_ack = seq + 1;
        http_seq++;
        tcp_send(0x10, 0, 0);
        u8 request[460];
        memset(request, 0, sizeof(request));
        copy((char *)request, "GET ", sizeof(request));
        copy((char *)request + strlen((char *)request), http_path,
             (int)(sizeof(request) - strlen((char *)request)));
        copy((char *)request + strlen((char *)request),
             " HTTP/1.0\r\nHost: ", (int)(sizeof(request) - strlen((char *)request)));
        copy((char *)request + strlen((char *)request), http_host,
             (int)(sizeof(request) - strlen((char *)request)));
        copy((char *)request + strlen((char *)request),
             "\r\nConnection: close\r\nUser-Agent: MayOS/0.4\r\n\r\n",
             (int)(sizeof(request) - strlen((char *)request)));
        u16 bytes = (u16)strlen((char *)request);
        tcp_send(0x18, request, bytes);
        http_seq += bytes;
        http_stage = 2;
        serial("MayNet: HTTP GET sent\n");
        return;
    }
    if (http_stage != 2) return;
    u16 payload = (u16)(length - hlen);
    if (payload) {
        if (seq != http_ack) return;
        http_ack += payload;
        const u8 *body = p + hlen;
        for (u16 i = 0; i < payload && http_used < (int)sizeof(http_response) - 2; i++) {
            u8 c = body[i];
            http_response[http_used++] =
                (c == '\n' || c == '\r' || c == '\t' || c >= 32) ? (char)c : '?';
        }
        http_response[http_used] = 0;
        tcp_send(0x10, 0, 0);
        serial("MayNet: HTTP response received\n");
    }
    if (flags & 1) {
        http_ack = seq + payload + 1;
        tcp_send(0x10, 0, 0);
        http_stage = 3;
        serial("MayNet: HTTP connection closed\n");
    }
    if (flags & 4) http_stage = 3;
}
static int parse_address(const char *input, u32 *address, const char **end) {
    u32 result = 0;
    const char *p = input;
    for (int i = 0; i < 4; i++) {
        u32 value = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 3) {
            value = value * 10 + (u32)(*p++ - '0');
            digits++;
        }
        if (!digits || value > 255) return 0;
        result = (result << 8) | value;
        if (i != 3) {
            if (*p++ != '.') return 0;
        }
    }
    *address = result;
    *end = p;
    return 1;
}
int net_http_start(const char *url) {
    http_stage = http_used = 0;
    http_response[0] = 0;
    if (!link_ready || !lease_ready || !gateway_ready) {
        copy(http_response, "Network not ready. Retry DHCP / ARP.", sizeof(http_response));
        return 0;
    }
    if (!url || strlen(url) > 320 || url[0] != 'h' || url[1] != 't' ||
        url[2] != 't' || url[3] != 'p' || url[4] != ':' ||
        url[5] != '/' || url[6] != '/') {
        copy(http_response, "Use http://IPv4:port/path. HTTPS is unsupported.",
             sizeof(http_response));
        return 0;
    }
    const char *p = url + 7, *host_end = p;
    if (!parse_address(p, &http_ip, &host_end)) {
        copy(http_response, "Use an IPv4 address. Hostname HTTP is not yet supported.",
             sizeof(http_response));
        return 0;
    }
    http_port = 80;
    p = host_end;
    if (*p == ':') {
        p++;
        u32 port = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 5) {
            port = port * 10 + (u32)(*p++ - '0');
            digits++;
        }
        if (!digits || port == 0 || port > 65535) {
            copy(http_response, "Invalid HTTP port.", sizeof(http_response));
            return 0;
        }
        http_port = (u16)port;
    }
    if (*p && *p != '/') {
        copy(http_response, "Invalid HTTP address.", sizeof(http_response));
        return 0;
    }
    copy(http_host, url + 7, sizeof(http_host));
    for (int i = 0; http_host[i]; i++)
        if (http_host[i] == '/') { http_host[i] = 0; break; }
    copy(http_path, *p ? p : "/", sizeof(http_path));
    for (int i = 0; http_path[i]; i++)
        if ((u8)http_path[i] < 33 || (u8)http_path[i] >= 127) {
            copy(http_response, "Invalid HTTP path.", sizeof(http_response));
            return 0;
        }
    /* Each new connection needs a distinct TCP source port / initial sequence.
       Reusing a 4-tuple immediately after FIN may be rejected by NAT TIME_WAIT. */
    http_source_port = (u16)(53000u + (++identifier % 2000u));
    http_seq = 0x504d4159u ^ ticks ^ identifier;
    http_ack = 0;
    http_stage = 1;
    http_started = ticks;
    tcp_send(0x02, 0, 0);
    serial("MayNet: TCP SYN sent\n");
    return 1;
}
int net_http_poll(char *destination, int capacity) {
    if (http_stage == 1 || http_stage == 2) {
        if (ticks - http_started > 1200) {
            if (!http_used)
                copy(http_response, "HTTP connection timed out.", sizeof(http_response));
            http_stage = 3;
        }
    }
    if (destination && capacity > 0)
        copy(destination, http_response, capacity);
    return http_stage == 1 || http_stage == 2;
}

static void receive(const u8 *p, u16 length) {
    if (length < 14) return;
    u16 type = be16(p + 12);
    if (type == ETH_ARP) { arp_receive(p, length); return; }
    if (type != ETH_IP || length < 34) return;
    const u8 *h = p + 14;
    u32 size = (u32)(h[0] & 15u) * 4u;
    if ((h[0] >> 4) != 4 || size < 20 || 14u + size > length ||
        be16(h + 2) < size || 14u + be16(h + 2) > length) return;
    if (checksum(h, size)) return;
    const u8 *body = h + size;
    u16 body_len = (u16)(be16(h + 2) - size);
    if (h[9] == IP_UDP) udp_receive(body, body_len);
    else if (h[9] == IP_ICMP) icmp_receive(body, body_len);
    else if (h[9] == 6) tcp_receive(body, body_len, be32(h + 12));
}
static u32 pci_read(u8 bus, u8 slot, u8 function, u8 offset) {
    outl(0xcf8, 0x80000000u | (u32)bus << 16 | (u32)slot << 11 |
          (u32)function << 8 | (offset & 0xfcu));
    return inl(0xcfc);
}
static void pci_write(u8 bus, u8 slot, u8 function, u8 offset, u32 value) {
    outl(0xcf8, 0x80000000u | (u32)bus << 16 | (u32)slot << 11 |
          (u32)function << 8 | (offset & 0xfcu));
    outl(0xcfc, value);
}
void net_init(void) {
    for (u32 bus = 0; bus < 256 && !nic_io; bus++)
        for (u32 slot = 0; slot < 32 && !nic_io; slot++)
            for (u32 function = 0; function < 8 && !nic_io; function++) {
                u32 id = pci_read((u8)bus, (u8)slot, (u8)function, 0);
                if (id != 0x813910ecu) continue;
                u32 bar = pci_read((u8)bus, (u8)slot, (u8)function, 0x10);
                if (!(bar & 1u)) continue;
                nic_io = (u16)(bar & ~3u);
                u32 command = pci_read((u8)bus, (u8)slot, (u8)function, 4);
                pci_write((u8)bus, (u8)slot, (u8)function, 4,
                          command | 5u);
            }
    if (!nic_io) {
        serial("MayNet: RTL8139 adapter unavailable\n");
        return;
    }
    outb((u16)(nic_io + 0x52), 0);
    outb((u16)(nic_io + 0x37), 0x10);
    for (u32 i = 0; i < 100000 && (inb((u16)(nic_io + 0x37)) & 0x10); i++) {}
    if (inb((u16)(nic_io + 0x37)) & 0x10) {
        nic_io = 0;
        serial("MayNet: RTL8139 reset failed\n");
        return;
    }
    for (int i = 0; i < 6; i++) mac[i] = inb((u16)(nic_io + i));
    memset(rx_ring, 0, sizeof(rx_ring));
    rx_at = tx_slot = 0;
    outl((u16)(nic_io + 0x30), (u32)rx_ring);
    outw((u16)(nic_io + 0x3c), 0);
    outw((u16)(nic_io + 0x3e), 0xffffu);
    outl((u16)(nic_io + 0x44), 0xf);
    outb((u16)(nic_io + 0x37), 0x0c);
    link_ready = 1;
    dhcp_phase = 1;
    serial("MayNet: RTL8139 ready\n");
    dhcp_send(0);
}
void net_poll(void) {
    if (!link_ready) return;
    for (int tries = 0; tries < 12 && !(inb((u16)(nic_io + 0x37)) & 1); tries++) {
        u16 flags = read_ring16(rx_at);
        u16 len = read_ring16((u16)(rx_at + 2u));
        if (!(flags & 1u) || len < 18 || len > 1536) {
            /* Recover the software read pointer from RTL8139's current DMA
               write pointer instead of retrying the same corrupt descriptor
               indefinitely. Keep the adapter active and acknowledge IRQs. */
            u16 dma = inw((u16)(nic_io + 0x3a));
            rx_at = (u16)((dma + 3u) & 0xfffcu);
            outw((u16)(nic_io + 0x38), (u16)(rx_at - 16u));
            outw((u16)(nic_io + 0x3e), 0xffffu);
            serial("MayNet: RX resynchronized\n");
            return;
        }
        for (u16 i = 0; i < len - 4; i++)
            frame[i] = rx_ring[(rx_at + 4u + i) & 65535u];
        rx_at = (u16)((rx_at + len + 4u + 3u) & ~3u);
        outw((u16)(nic_io + 0x38), (u16)(rx_at - 16u));
        receive(frame, len - 4);
    }
    outw((u16)(nic_io + 0x3e), 0xffffu);
    if (!lease_ready && ticks - last_dhcp > 300) {
        dhcp_phase = 1;
        dhcp_send(0);
    }
    if (lease_ready && gateway && !gateway_ready && ticks - last_arp > 300) {
        last_arp = ticks;
        arp_request(gateway);
    }
}
int net_online(void) { return lease_ready; }
void net_ping(void) {
    if (!lease_ready || !gateway) {
        serial("MayNet: no IPv4 lease/gateway\n");
        return;
    }
    if (!gateway_ready) {
        arp_request(gateway);
        serial("MayNet: resolving gateway MAC\n");
        return;
    }
    u8 packet[16] = {8, 0, 0, 0, 0x4d, 0x59, 0, 1,
                     'M','a','y','O','S','4','0','0'};
    wr16(packet + 2, checksum(packet, sizeof(packet)));
    int length = ipv4(frame, gateway_mac, ip_addr, gateway, IP_ICMP,
                      packet, sizeof(packet));
    if (length) tx(frame, (u16)length);
    serial("MayNet: ICMP echo sent\n");
}
void net_dns(void) {
    if (!lease_ready || !dns_server) {
        serial("MayNet: no DHCP DNS server\n");
        return;
    }
    if (!gateway_ready) { arp_request(gateway); return; }
    u8 query[64];
    memset(query, 0, sizeof(query));
    wr16(query, 0x4d59);
    wr16(query + 2, 0x0100);
    wr16(query + 4, 1);
    int at = 12;
    query[at++] = 7; memcpy(query + at, "example", 7); at += 7;
    query[at++] = 3; memcpy(query + at, "com", 3); at += 3;
    query[at++] = 0;
    wr16(query + at, 1); at += 2;
    wr16(query + at, 1); at += 2;
    udp_send(gateway_mac, ip_addr, dns_server, 53000, 53, query, (u16)at);
    serial("MayNet: DNS A query sent\n");
}
void net_status(void) {
    if (!link_ready) { serial("MayNet: offline (RTL8139 missing)\n"); return; }
    if (!lease_ready) { serial("MayNet: obtaining IPv4 via DHCP\n"); return; }
    char b[24];
    serial("MayNet: link up, IPv4 ");
    for (int i = 3; i >= 0; i--) {
        number(b, (ip_addr >> (i * 8)) & 255u);
        serial(b);
        serial(i ? "." : "\n");
    }
    serial("MayNet: ping replies=");
    number(b, (u32)ping_replies);
    serial(b);
    serial("; DNS replies=");
    number(b, (u32)dns_replies);
    serial(b);
    serial("\n");
}
void net_command(const char *argument) {
    if (!strcmp(argument, "ping")) net_ping();
    else if (!strcmp(argument, "dns")) net_dns();
    else if (!strcmp(argument, "renew")) {
        lease_ready = gateway_ready = 0; dhcp_phase = 1; dhcp_send(0);
    } else net_status();
}

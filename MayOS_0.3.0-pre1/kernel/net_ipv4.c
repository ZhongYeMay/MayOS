#include "net_ipv4.h"

/* Internet one's-complement sum. The function also verifies a header when
 * invoked over an existing header including its checksum. */
static u32 add_words(const u8 *p, u32 length, u32 sum) {
    for (u32 i = 0; i < length; i += 2u) {
        u32 value = (u32)p[i] << 8;
        if (i + 1u < length) value |= p[i + 1u];
        sum += value;
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return sum;
}
static u16 complement(u32 sum) {
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (u16)~sum;
}
u16 may_net_checksum(const u8 *data, u32 length) {
    return complement(add_words(data, length, 0u));
}
static u16 read16(const u8 *p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}
static void put16(u8 *p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}
static u32 udp_sum(const u8 *ip, const u8 *udp, u16 length) {
    u32 sum = 0;
    sum = add_words(ip + 12, 8, sum);
    sum += 17u + length; /* pseudoheader: zero, protocol, UDP size */
    return add_words(udp, length, sum);
}
int may_ipv4_udp_build(u8 *frame, u32 capacity, const u8 source_mac[6],
                       const u8 destination_mac[6], const u8 source_ip[4],
                       const u8 destination_ip[4], u16 source_port,
                       u16 destination_port, const u8 *payload,
                       u16 payload_length, u16 ipv4_id) {
    const u32 ip_size = 20u + 8u + payload_length;
    const u32 frame_bytes = 14u + ip_size;
    const u32 wire_bytes = frame_bytes < 60u ? 60u : frame_bytes;
    if (!frame || !source_mac || !destination_mac ||
        !source_ip || !destination_ip || !source_port ||
        !destination_port || (payload_length && !payload) ||
        ip_size > 1500u || capacity < wire_bytes) return 0;
    memset(frame, 0, wire_bytes);
    memcpy(frame, destination_mac, 6);
    memcpy(frame + 6, source_mac, 6);
    frame[12] = 8; frame[13] = 0; /* Ethernet IPv4 */
    u8 *ip = frame + 14;
    ip[0] = 0x45;   /* IPv4, no options */
    ip[1] = 0;      /* DSCP / ECN */
    put16(ip + 2, (u16)ip_size);
    put16(ip + 4, ipv4_id);
    put16(ip + 6, 0x4000); /* Don't fragment */
    ip[8] = 64;
    ip[9] = 17;
    memcpy(ip + 12, source_ip, 4);
    memcpy(ip + 16, destination_ip, 4);
    put16(ip + 10, may_net_checksum(ip, 20));
    u8 *udp = ip + 20;
    put16(udp, source_port);
    put16(udp + 2, destination_port);
    put16(udp + 4, (u16)(8u + payload_length));
    if (payload_length) memcpy(udp + 8, payload, payload_length);
    u16 sum = complement(udp_sum(ip, udp, (u16)(8u + payload_length)));
    put16(udp + 6, sum ? sum : 0xffffu);
    return (int)wire_bytes;
}
static int parse_ipv4_udp(const u8 *frame, u32 frame_length,
                          const u8 expected_mac[6], const u8 expected_ip[4],
                          MayUdpDatagram *result, int dhcp) {
    if (!frame || !expected_mac || !expected_ip || !result ||
        frame_length < 14u + 20u + 8u ||
        frame[12] != 8 || frame[13] != 0) return 0;
    int own_mac = 1, broadcast_mac = 1;
    for (int i = 0; i < 6; i++) {
        if (frame[i] != expected_mac[i]) own_mac = 0;
        if (frame[i] != 0xffu) broadcast_mac = 0;
    }
    if (!own_mac && !(dhcp && broadcast_mac)) return 0;
    const u8 *ip = frame + 14;
    if (ip[0] != 0x45 || ip[9] != 17 || !ip[8] ||
        (read16(ip + 6) & 0x3fffu) ||
        read16(ip + 2) < 28u || read16(ip + 2) > 1500u ||
        14u + (u32)read16(ip + 2) > frame_length ||
        may_net_checksum(ip, 20) != 0) return 0;
    int own_ip = 1, broadcast_ip = 1;
    for (int i = 0; i < 4; i++) {
        if (ip[16 + i] != expected_ip[i]) own_ip = 0;
        if (ip[16 + i] != 255u) broadcast_ip = 0;
    }
    if (!own_ip && !(dhcp && broadcast_ip)) return 0;
    const u8 *udp = ip + 20;
    u16 size = read16(udp + 4);
    if (size < 8u || size != read16(ip + 2) - 20u ||
        !read16(udp) || !read16(udp + 2) ||
        (!read16(udp + 6) && !dhcp) ||
        (read16(udp + 6) && complement(udp_sum(ip, udp, size)) != 0))
        return 0;
    if (dhcp && (read16(udp) != 67u || read16(udp + 2) != 68u))
        return 0;
    memset(result, 0, sizeof(*result));
    memcpy(result->source_ip, ip + 12, 4);
    memcpy(result->destination_ip, ip + 16, 4);
    result->source_port = read16(udp);
    result->destination_port = read16(udp + 2);
    result->payload = udp + 8;
    result->payload_length = (u16)(size - 8u);
    return 1;
}
int may_ipv4_udp_parse(const u8 *frame, u32 frame_length,
                       const u8 expected_mac[6], const u8 expected_ip[4],
                       MayUdpDatagram *result) {
    return parse_ipv4_udp(frame,frame_length,expected_mac,expected_ip,result,0);
}
int may_ipv4_udp_parse_dhcp(const u8 *frame, u32 frame_length,
                            const u8 local_mac[6], const u8 offered_ip[4],
                            MayUdpDatagram *result) {
    return parse_ipv4_udp(frame,frame_length,local_mac,offered_ip,result,1);
}
int may_dns_query_a(u8 *destination, u32 capacity, const char *hostname,
                    u16 transaction_id) {
    if (!destination || !hostname || !transaction_id || capacity < 18u)
        return 0;
    u32 size = 12;
    memset(destination, 0, capacity < 270u ? capacity : 270u);
    put16(destination, transaction_id);
    destination[2] = 1; /* recursion desired */
    destination[5] = 1; /* QDCOUNT */
    const char *p = hostname;
    u32 total = 0;
    while (*p) {
        const char *first = p;
        u32 part = 0;
        while (*p && *p != '.') {
            char ch = *p;
            if (!(ch >= 'a' && ch <= 'z') &&
                !(ch >= 'A' && ch <= 'Z') &&
                !(ch >= '0' && ch <= '9') && ch != '-')
                return 0;
            if (part == 0 && ch == '-') return 0;
            if (part >= 63u) return 0;
            p++; part++; total++;
        }
        if (!part || p[-1] == '-' || total > 253u ||
            size + 1u + part + 5u > capacity) return 0;
        destination[size++] = (u8)part;
        for (u32 i = 0; i < part; i++) destination[size++] = (u8)first[i];
        if (*p == '.') {
            p++;
            if (!*p) return 0;
        }
        if (size > 255u) return 0;
    }
    if (!total || size + 5u > capacity) return 0;
    destination[size++] = 0;
    destination[size++] = 0; destination[size++] = 1; /* A */
    destination[size++] = 0; destination[size++] = 1; /* IN */
    return (int)size;
}
/* Skips the name in DNS message but never follows arbitrary compression
 * pointers. This bounds all parsing and avoids compression loops. */
static int skip_name(const u8 *p, u32 length, u32 *cursor) {
    u32 x = *cursor;
    for (u32 label = 0; label < 128u; label++) {
        if (x >= length) return 0;
        u8 n = p[x++];
        if (!n) { *cursor = x; return 1; }
        if ((n & 0xc0u) == 0xc0u) {
            if (x >= length || ((((u32)n & 0x3fu) << 8) | p[x]) >= length)
                return 0;
            *cursor = x + 1u;
            return 1;
        }
        if ((n & 0xc0u) || n > 63u || x + n > length) return 0;
        x += n;
    }
    return 0;
}
int may_dns_reply_a(const u8 *p, u32 length, u16 transaction_id, u8 address[4]) {
    if (!p || !address || length < 12u || length > 512u ||
        read16(p) != transaction_id ||
        (p[2] & 0x80u) == 0 || (p[2] & 0x02u) ||
        (p[3] & 0x0fu) != 0 || read16(p + 4) != 1 ||
        read16(p + 6) == 0) return 0;
    u32 cursor = 12;
    if (!skip_name(p, length, &cursor) || cursor + 4u > length ||
        read16(p + cursor) != 1 || read16(p + cursor + 2) != 1) return 0;
    cursor += 4;
    u16 count = read16(p + 6);
    if (count > 16u) return 0;
    for (u16 i = 0; i < count; i++) {
        if (!skip_name(p, length, &cursor) || cursor + 10u > length) return 0;
        u16 type = read16(p + cursor);
        u16 class_id = read16(p + cursor + 2);
        u16 data_len = read16(p + cursor + 8);
        cursor += 10;
        if (cursor + data_len > length) return 0;
        if (type == 1 && class_id == 1 && data_len == 4) {
            memcpy(address, p + cursor, 4);
            return 1;
        }
        cursor += data_len;
    }
    return 0;
}

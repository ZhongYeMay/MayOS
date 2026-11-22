#include "net_ipv4.h"

/* Host-build deterministic tests without Linux networking or libc.
 * Exercise malformed lengths, checksum failures and DNS parsing. */
void *memset(void *dst, int value, u32 count) {
    u8 *d = dst;
    for (u32 i = 0; i < count; i++) d[i] = (u8)value;
    return dst;
}
void *memcpy(void *dst, const void *src, u32 count) {
    u8 *d = dst;
    const u8 *s = src;
    for (u32 i = 0; i < count; i++) d[i] = s[i];
    return dst;
}
int main(void) {
    u8 a_mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56};
    u8 b_mac[6] = {0x52, 0x55, 0, 0x11, 0x22, 0x33};
    u8 a_ip[4] = {10, 0, 2, 15}, b_ip[4] = {10, 0, 2, 2};
    u8 payload[6] = {'m','a','y','o','s','!'};
    u8 frame[1536];
    MayUdpDatagram got;
    int length = may_ipv4_udp_build(frame, sizeof(frame), a_mac, b_mac,
                                    a_ip, b_ip, 40000, 10053,
                                    payload, sizeof(payload), 1);
    if (length != 60 || may_net_checksum(frame + 14, 20)) return 1;
    if (!may_ipv4_udp_parse(frame, (u32)length, b_mac, b_ip, &got)) return 2;
    if (got.payload_length != sizeof(payload) ||
        got.source_port != 40000 || got.destination_port != 10053) return 3;
    for (u32 i = 0; i < sizeof(payload); i++)
        if (got.payload[i] != payload[i]) return 4;
    frame[14 + 20 + 8] ^= 0x01;
    if (may_ipv4_udp_parse(frame, (u32)length, b_mac, b_ip, &got)) return 5;
    frame[14 + 20 + 8] ^= 0x01;
    frame[14 + 6] = 0x20; /* unsupported fragment */
    if (may_ipv4_udp_parse(frame, (u32)length, b_mac, b_ip, &got)) return 6;
    frame[14 + 6] = 0x40;
    frame[14 + 2] = 0xff; /* inflated IP total length */
    if (may_ipv4_udp_parse(frame, (u32)length, b_mac, b_ip, &got)) return 7;
    length = may_ipv4_udp_build(frame, sizeof(frame), a_mac, b_mac,
                                a_ip, b_ip, 40000, 10053,
                                payload, sizeof(payload), 2);
    if (!length || may_ipv4_udp_parse(frame, (u32)(length - 20),
                                     b_mac, b_ip, &got)) return 8;
    u8 query[512], response[512], answer[4] = {0};
    int n = may_dns_query_a(query, sizeof(query), "example.test", 0x1234);
    if (n != 30 || query[0] != 0x12 || query[1] != 0x34 ||
        query[12] != 7 || query[20] != 4 ||
        query[29] != 1) return 9;
    memcpy(response, query, (u32)n);
    response[2] = 0x81; response[3] = 0x80;
    response[6] = 0; response[7] = 1; /* one A answer */
    response[n++] = 0xc0; response[n++] = 0x0c;
    response[n++] = 0; response[n++] = 1;
    response[n++] = 0; response[n++] = 1;
    response[n++] = 0; response[n++] = 0;
    response[n++] = 0; response[n++] = 60; /* TTL */
    response[n++] = 0; response[n++] = 4;
    response[n++] = 203; response[n++] = 0;
    response[n++] = 113; response[n++] = 7;
    if (!may_dns_reply_a(response, (u32)n, 0x1234, answer)) return 10;
    if (answer[0] != 203 || answer[1] != 0 ||
        answer[2] != 113 || answer[3] != 7) return 11;
    if (may_dns_reply_a(response, (u32)n, 0x1235, answer)) return 12;
    response[3] = 0x83; /* NXDOMAIN */
    if (may_dns_reply_a(response, (u32)n, 0x1234, answer)) return 13;
    response[3] = 0x80;
    if (may_dns_reply_a(response, (u32)n - 1u, 0x1234, answer)) return 14;
    if (may_dns_query_a(query, sizeof(query), "bad..name", 0x1234)) return 15;
    if (may_dns_query_a(query, sizeof(query), "-bad.example", 0x1234)) return 16;
    if (may_dns_query_a(query, 20, "example.test", 0x1234)) return 17;
    if (may_ipv4_udp_build(frame, 42, a_mac, b_mac, a_ip, b_ip, 40000,
                           10053, payload, sizeof(payload), 4)) return 18;
    u8 mask[4] = {255, 255, 255, 0};
    u8 remote[4] = {203, 0, 113, 9}, hop[4];
    if (!may_ipv4_next_hop(a_ip, mask, b_ip, b_ip, hop) || hop[3] != 2) return 19;
    if (!may_ipv4_next_hop(a_ip, mask, b_ip, remote, hop) || hop[0] != 10 || hop[3] != 2) return 20;
    if (may_ipv4_next_hop(a_ip, mask, (const u8[4]){0}, remote, hop)) return 21;
    if (!may_ipv4_next_hop(a_ip, (const u8[4]){0}, b_ip, remote, hop) || hop[3] != 2) return 22;
    return 0;
}

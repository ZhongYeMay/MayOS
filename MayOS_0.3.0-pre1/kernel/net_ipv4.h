#ifndef MAYOS_NET_IPV4_H
#define MAYOS_NET_IPV4_H
#include "core.h"

/* Pure, freestanding IPv4/UDP and DNS-A codecs. No NIC or host ABI required.
 * Deliberately no fragments, IP options, TLS, TCP, or public DNS trust. */
typedef struct {
    u8 source_ip[4], destination_ip[4];
    u16 source_port, destination_port;
    const u8 *payload;
    u16 payload_length;
} MayUdpDatagram;

u16 may_net_checksum(const u8 *data, u32 length);
int may_ipv4_udp_build(u8 *frame, u32 capacity, const u8 source_mac[6],
                       const u8 destination_mac[6], const u8 source_ip[4],
                       const u8 destination_ip[4], u16 source_port,
                       u16 destination_port, const u8 *payload,
                       u16 payload_length, u16 ipv4_id);
int may_ipv4_udp_parse(const u8 *frame, u32 frame_length,
                       const u8 expected_mac[6], const u8 expected_ip[4],
                       MayUdpDatagram *result);
int may_dns_query_a(u8 *destination, u32 capacity, const char *hostname,
                    u16 transaction_id);
int may_dns_reply_a(const u8 *packet, u32 length, u16 transaction_id,
                    u8 address[4]);

#endif

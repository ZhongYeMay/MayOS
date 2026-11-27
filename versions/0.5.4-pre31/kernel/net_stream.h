#ifndef MAYOS_NET_STREAM_H
#define MAYOS_NET_STREAM_H
#include "net_tcp.h"
#include "net_dhcp.h"

#include "net_stream_io.h"

/* Internal Ethernet integration. Routes are tied to the active DHCP lease. */
void may_stream_poll(void);
void may_stream_address_lost(void);
void may_stream_arp(const u8 address[4], const u8 mac[6]);
int may_stream_receive(const MayTcpSegment *segment);
int may_stream_send_arp(const u8 next_hop[4]);
int may_stream_send_segment(const u8 mac[6], const u8 address[4], u16 source_port,
                            u16 port, u32 seq, u32 ack, u8 flags, u16 window,
                            const u8 *data, u16 length);
#endif

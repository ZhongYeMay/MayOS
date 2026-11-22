#ifndef MAYOS_NET_STREAM_H
#define MAYOS_NET_STREAM_H
#include "net_tcp.h"
#include "net_dhcp.h"

/* One nonblocking outbound IPv4 stream. A zero read/write means try again.
 * Receive EOF and failure are reported separately from a temporarily empty buffer. */
enum { MAY_STREAM_IDLE, MAY_STREAM_CONNECTING, MAY_STREAM_READY,
       MAY_STREAM_EOF, MAY_STREAM_CLOSED, MAY_STREAM_FAILED };
int may_stream_open(const u8 destination[4], u16 port);
int may_stream_status(void);
int may_stream_read(u8 *data, unsigned capacity);
int may_stream_write(const u8 *data, unsigned length);
void may_stream_close(void);
int may_stream_echo_check(const char *address, const char *port);

/* Internal Ethernet integration. Routes are tied to the active DHCP lease. */
void may_stream_poll(void);
void may_stream_arp(const u8 address[4], const u8 mac[6]);
int may_stream_receive(const MayTcpSegment *segment);
int may_stream_send_arp(const u8 next_hop[4]);
int may_stream_send_segment(const u8 mac[6], const u8 address[4], u16 source_port,
                            u16 port, u32 seq, u32 ack, u8 flags, u16 window,
                            const u8 *data, u16 length);
#endif

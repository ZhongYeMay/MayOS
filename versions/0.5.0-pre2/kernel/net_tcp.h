#ifndef MAYOS_NET_TCP_H
#define MAYOS_NET_TCP_H
#include "core.h"

/* Minimal native TCP packet codec. No congestion/retransmission machinery:
 * a QEMU localhost diagnostic only, not production-quality TCP. */
enum {
    MAY_TCP_FIN=0x01, MAY_TCP_SYN=0x02, MAY_TCP_RST=0x04,
    MAY_TCP_PSH=0x08, MAY_TCP_ACK=0x10
};
typedef struct {
    u8 source_ip[4],destination_ip[4];
    u16 source_port,destination_port,window;
    u32 seq,ack;
    u8 flags;
    const u8 *payload;
    u16 payload_length;
} MayTcpSegment;
int may_tcp_build(u8 *frame,u32 capacity,
                  const u8 src_mac[6],const u8 dst_mac[6],
                  const u8 src_ip[4],const u8 dst_ip[4],
                  u16 src_port,u16 dst_port,u32 seq,u32 ack,
                  u8 flags,u16 window,const u8 *payload,u16 length,
                  u16 ipv4_id);
int may_tcp_parse(const u8 *frame,u32 frame_length,
                  const u8 local_mac[6],const u8 local_ip[4],
                  MayTcpSegment *result);
#endif

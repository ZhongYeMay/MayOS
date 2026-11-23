#ifndef MAYOS_TCP_RETRY_H
#define MAYOS_TCP_RETRY_H
#include "core.h"

/* One outstanding TCP segment, small QEMU-only diagnostic. One hundred
 * interrupt ticks ~= 1 second; tick subtraction supports u32 wraparound.
 * This is NOT a production TCP congestion-control or retransmission stack. */
enum { MAY_TCP_RETRY_TICKS=100, MAY_TCP_RETRY_LIMIT=3 };
typedef struct {
    u32 first_seq, expected_ack, last_sent_tick;
    u8 flags, active, attempts;
    u16 payload_length;
} MayTcpRetry;

void may_tcp_retry_start(MayTcpRetry *r,u32 seq,u16 size,u8 flags,u32 now);
int may_tcp_retry_ack(MayTcpRetry *r,u32 acknowledgement);
int may_tcp_retry_due(const MayTcpRetry *r,u32 now);
int may_tcp_retry_exhausted(const MayTcpRetry *r,u32 now);
void may_tcp_retry_resent(MayTcpRetry *r,u32 now);
void may_tcp_retry_cancel(MayTcpRetry *r);

#endif

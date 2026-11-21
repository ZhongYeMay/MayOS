#include "net_tcp_retry.h"

void may_tcp_retry_start(MayTcpRetry *r,u32 seq,u16 size,u8 flags,u32 now) {
    if(!r)return;
    memset(r,0,sizeof(*r));
    r->first_seq=seq;
    r->expected_ack=seq+(u32)size+
        ((flags&0x02u)?1u:0u)+((flags&0x01u)?1u:0u);
    r->last_sent_tick=now;
    r->payload_length=size;
    r->flags=flags;
    r->active=1;
}
int may_tcp_retry_ack(MayTcpRetry *r,u32 ack) {
    if(!r||!r->active||ack!=r->expected_ack)return 0;
    r->active=0;
    return 1;
}
int may_tcp_retry_due(const MayTcpRetry *r,u32 now) {
    return r && r->active && r->attempts<MAY_TCP_RETRY_LIMIT &&
           (u32)(now-r->last_sent_tick)>=MAY_TCP_RETRY_TICKS;
}
int may_tcp_retry_exhausted(const MayTcpRetry *r,u32 now) {
    return r && r->active && r->attempts>=MAY_TCP_RETRY_LIMIT &&
           (u32)(now-r->last_sent_tick)>=MAY_TCP_RETRY_TICKS;
}
void may_tcp_retry_resent(MayTcpRetry *r,u32 now) {
    if(!may_tcp_retry_due(r,now))return;
    r->last_sent_tick=now;
    r->attempts++;
}
void may_tcp_retry_cancel(MayTcpRetry *r) {
    if(r)r->active=0;
}

#include "net_tcp_retry.h"

void *memset(void *dst,int v,u32 n) {
    u8 *p=dst;for(u32 i=0;i<n;i++)p[i]=(u8)v;
    return dst;
}
int main(void) {
    MayTcpRetry r;
    may_tcp_retry_start(&r,100,0,2,50);
    if(!r.active||r.expected_ack!=101||r.attempts) return 1;
    if(may_tcp_retry_due(&r,149)||!may_tcp_retry_due(&r,150))return 2;
    if(may_tcp_retry_ack(&r,100)||!r.active)return 3;
    may_tcp_retry_resent(&r,150);
    if(r.attempts!=1||r.last_sent_tick!=150)return 4;
    if(may_tcp_retry_due(&r,200)||!may_tcp_retry_due(&r,250))return 5;
    if(!may_tcp_retry_ack(&r,101)||r.active||may_tcp_retry_due(&r,999))
        return 6;
    may_tcp_retry_start(&r,0xfffffff0u,5,0x18,0xfffffff0u);
    if(r.expected_ack!=0xfffffff5u)return 7;
    if(may_tcp_retry_due(&r,50)||!may_tcp_retry_due(&r,85))
        return 8; /* timer ticks wrap */
    may_tcp_retry_start(&r,90,16,0x18,0);
    for(u32 i=1;i<=3;i++) {
        if(!may_tcp_retry_due(&r,i*100))return 9;
        may_tcp_retry_resent(&r,i*100);
        if(r.attempts!=i)return 10;
    }
    if(may_tcp_retry_due(&r,400)||!may_tcp_retry_exhausted(&r,400))
        return 11;
    may_tcp_retry_cancel(&r);
    if(r.active||may_tcp_retry_exhausted(&r,1000))return 12;
    may_tcp_retry_start(&r,100,0,1,100);
    if(r.expected_ack!=101)return 13; /* FIN occupies one sequence */
    return 0;
}

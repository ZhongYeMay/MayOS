#include "net_tcp.h"
#include "net_ipv4.h"
void *memset(void *dest,int value,u32 n) {
    u8 *p=dest;for(u32 i=0;i<n;i++)p[i]=(u8)value;return dest;
}
void *memcpy(void *dest,const void *src,u32 n) {
    u8 *p=dest;const u8 *s=src;
    for(u32 i=0;i<n;i++)p[i]=s[i];
    return dest;
}
int main(void) {
    const u8 srcmac[6]={0x52,0x54,0,0x33,0x44,0x55};
    const u8 dstmac[6]={0x52,0x54,0,0x11,0x22,0x33};
    const u8 srcip[4]={10,0,2,2};
    const u8 dstip[4]={10,0,2,15};
    const char data[]="HTTP/1.1 200 OK";
    u8 frame[1536];
    MayTcpSegment segment;
    int n=may_tcp_build(frame,sizeof(frame),srcmac,dstmac,
        srcip,dstip,10080,40100,345,101,
        MAY_TCP_PSH|MAY_TCP_ACK,4096,(const u8 *)data,
        sizeof(data)-1u,35);
    if(n<=60||!may_tcp_parse(frame,(u32)n,dstmac,dstip,&segment))
        return 1;
    if(segment.seq!=345||segment.ack!=101||
       segment.source_port!=10080||segment.destination_port!=40100||
       segment.flags!=(MAY_TCP_PSH|MAY_TCP_ACK)||
       segment.payload_length!=sizeof(data)-1u)return 2;
    for(u32 i=0;i<sizeof(data)-1u;i++)
        if(segment.payload[i]!=(u8)data[i])return 3;
    frame[n-1]^=1;
    if(may_tcp_parse(frame,(u32)n,dstmac,dstip,&segment))return 4;
    frame[n-1]^=1;
    frame[14+20+12]=0x40; /* invalid TCP data offset */
    if(may_tcp_parse(frame,(u32)n,dstmac,dstip,&segment))return 5;
    frame[14+20+12]=0x50;
    frame[14+2]=0xff; /* invalid IPv4 total length */
    if(may_tcp_parse(frame,(u32)n,dstmac,dstip,&segment))return 6;
    n=may_tcp_build(frame,sizeof(frame),srcmac,dstmac,srcip,dstip,
        10080,40100,345,101,MAY_TCP_SYN|MAY_TCP_ACK,4096,0,0,36);
    if(n!=60||!may_tcp_parse(frame,(u32)n,dstmac,dstip,&segment))
        return 7;
    if(may_tcp_build(frame,48,srcmac,dstmac,srcip,dstip,
        10080,40100,1,2,MAY_TCP_SYN,4096,0,0,36))return 8;
    return 0;
}

#include "net_tcp.h"
#include "net_ipv4.h"
static u16 r16(const u8 *p) {return (u16)(((u16)p[0]<<8)|p[1]);}
static u32 r32(const u8 *p) {
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void w16(u8 *p,u16 v) {p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void w32(u8 *p,u32 v) {
    p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;
}
static u32 sum_words(const u8 *p,u32 n,u32 s) {
    for(u32 i=0;i<n;i+=2u) {
        s+=(u32)p[i]<<8;
        if(i+1u<n)s+=p[i+1u];
        s=(s&0xffffu)+(s>>16);
    }
    return s;
}
static u16 sum_finish(u32 s) {
    while(s>>16)s=(s&0xffffu)+(s>>16);
    return (u16)~s;
}
static u16 tcp_sum(const u8 *ip,const u8 *tcp,u32 size) {
    u32 sum=sum_words(ip+12,8,0);
    sum+=6u+size;/* protocol number 6 and TCP payload size */
    return sum_finish(sum_words(tcp,size,sum));
}
int may_tcp_build(u8 *frame,u32 capacity,
                  const u8 src_mac[6],const u8 dst_mac[6],
                  const u8 src_ip[4],const u8 dst_ip[4],
                  u16 src_port,u16 dst_port,u32 seq,u32 ack,
                  u8 flags,u16 window,const u8 *payload,u16 length,
                  u16 ipv4_id) {
    u32 ip_bytes=20u+20u+(u32)length;
    u32 eth_bytes=14u+ip_bytes;
    u32 wire_bytes=eth_bytes<60u?60u:eth_bytes;
    if(!frame||!src_mac||!dst_mac||!src_ip||!dst_ip||
       !src_port||!dst_port||(length&&!payload)||
       length>1460u||capacity<wire_bytes||
       (flags&0xe0u))return 0;
    memset(frame,0,wire_bytes);
    memcpy(frame,dst_mac,6);memcpy(frame+6,src_mac,6);
    frame[12]=8;frame[13]=0;
    u8 *ip=frame+14;
    ip[0]=0x45;
    w16(ip+2,(u16)ip_bytes);
    w16(ip+4,ipv4_id);
    w16(ip+6,0x4000);/* DF */
    ip[8]=64;ip[9]=6;
    memcpy(ip+12,src_ip,4);memcpy(ip+16,dst_ip,4);
    w16(ip+10,may_net_checksum(ip,20));
    u8 *tcp=ip+20;
    w16(tcp,src_port);w16(tcp+2,dst_port);
    w32(tcp+4,seq);w32(tcp+8,ack);
    tcp[12]=0x50;/* TCP header length: 5 words */
    tcp[13]=flags;
    w16(tcp+14,window);
    if(length)memcpy(tcp+20,payload,length);
    u16 crc=tcp_sum(ip,tcp,20u+length);
    w16(tcp+16,crc);
    return (int)wire_bytes;
}
int may_tcp_parse(const u8 *frame,u32 frame_length,
                  const u8 local_mac[6],const u8 local_ip[4],
                  MayTcpSegment *r) {
    if(!frame||!local_mac||!local_ip||!r||
       frame_length<14u+20u+20u||frame[12]!=8||frame[13]!=0)
       return 0;
    for(u32 i=0;i<6;i++)if(frame[i]!=local_mac[i])return 0;
    const u8 *ip=frame+14;
    u16 ip_length=r16(ip+2);
    if(ip[0]!=0x45||ip[9]!=6||!ip[8]||
       (r16(ip+6)&0x3fffu)||ip_length<40u||ip_length>1500u||
       14u+(u32)ip_length>frame_length||
       may_net_checksum(ip,20)!=0)return 0;
    for(u32 i=0;i<4;i++)if(ip[16+i]!=local_ip[i])return 0;
    const u8 *tcp=ip+20;
    u32 tcp_bytes=(u32)ip_length-20u;
    u32 header=(u32)(tcp[12]>>4)*4u;
    if(header<20u||header>60u||header>tcp_bytes||
       !r16(tcp)||!r16(tcp+2)||tcp_sum(ip,tcp,tcp_bytes)!=0)
       return 0;
    memset(r,0,sizeof(*r));
    memcpy(r->source_ip,ip+12,4);
    memcpy(r->destination_ip,ip+16,4);
    r->source_port=r16(tcp);
    r->destination_port=r16(tcp+2);
    r->seq=r32(tcp+4);r->ack=r32(tcp+8);
    r->flags=tcp[13];
    r->window=r16(tcp+14);
    r->payload=tcp+header;
    r->payload_length=(u16)(tcp_bytes-header);
    return 1;
}

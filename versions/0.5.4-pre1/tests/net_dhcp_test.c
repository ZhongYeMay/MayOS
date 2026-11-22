#include "net_dhcp.h"

/* Host-side deterministic DHCPv4 codec tests, no libc or NIC needed. */
void *memset(void *dst,int v,u32 n) {
    u8 *d=dst; for(u32 i=0;i<n;i++) d[i]=(u8)v; return dst;
}
void *memcpy(void *dst,const void *src,u32 n) {
    u8 *d=dst; const u8 *s=src;
    for(u32 i=0;i<n;i++) d[i]=s[i];
    return dst;
}
static u8 mac[6]={0x52,0x54,0x00,0x12,0x34,0x56};
static const u8 server[4]={10,0,2,2};
static int packet(u8 *p, u8 type) {
    memset(p,0,300);
    p[0]=2; p[1]=1; p[2]=6;
    p[4]=0x12; p[5]=0x34; p[6]=0x56; p[7]=0x78;
    memcpy(p+28,mac,6);
    p[16]=10; p[17]=0; p[18]=2; p[19]=15;
    p[236]=99; p[237]=130; p[238]=83; p[239]=99;
    int i=240;
    p[i++]=53; p[i++]=1; p[i++]=type;
    p[i++]=54; p[i++]=4; memcpy(p+i,server,4); i+=4;
    p[i++]=1; p[i++]=4; p[i++]=255; p[i++]=255; p[i++]=255; p[i++]=0;
    p[i++]=3; p[i++]=4; memcpy(p+i,server,4); i+=4;
    p[i++]=6; p[i++]=4; p[i++]=10; p[i++]=0; p[i++]=2; p[i++]=3;
    p[i++]=51; p[i++]=4; p[i++]=0; p[i++]=0; p[i++]=0x0e; p[i++]=0x10;
    p[i++]=255;
    return 300;
}
int main(void) {
    MayDhcpClient c;
    u8 data[400],tmp[400];
    may_dhcp_begin(&c,mac,0x12345678);
    if(c.state!=MAY_DHCP_DISCOVER || may_dhcp_discover(&c,data,299)) return 1;
    if(may_dhcp_discover(&c,data,sizeof(data))!=300 ||
       data[0]!=1 || data[10]!=0x80 || data[242]!=1) return 2;
    packet(tmp,2);
    if(may_dhcp_receive(&c,tmp,239,server)) return 3;
    tmp[4]^=1;
    if(may_dhcp_receive(&c,tmp,300,server)) return 4;
    tmp[4]^=1;
    tmp[28]^=1;
    if(may_dhcp_receive(&c,tmp,300,server)) return 5;
    tmp[28]^=1;
    if(may_dhcp_receive(&c,tmp,300,(const u8[4]){10,0,2,99})) return 6;
    if(may_dhcp_receive(&c,tmp,300,server)!=1 || c.state!=MAY_DHCP_OFFER)
        return 7;
    if(may_dhcp_discover(&c,data,sizeof(data))) return 8;
    if(may_dhcp_request(&c,data,sizeof(data))!=300 ||
       c.state!=MAY_DHCP_REQUEST || data[242]!=3) return 9;
    if(may_dhcp_request(&c,data,sizeof(data))) return 10;
    packet(tmp,5);
    tmp[19]=99; /* offered IP mismatch */
    if(may_dhcp_receive(&c,tmp,300,server)) return 11;
    tmp[19]=15;
    tmp[247]=4; /* server ID altered */
    if(may_dhcp_receive(&c,tmp,300,server)) return 12;
    tmp[247]=2;
    if(may_dhcp_receive(&c,tmp,300,server)!=2 ||
       c.state!=MAY_DHCP_BOUND || c.lease_seconds!=3600 ||
       c.address[3]!=15 || c.dns[3]!=3) return 13;
    if(may_dhcp_receive(&c,tmp,300,server)) return 14;
    may_dhcp_begin(&c,mac,0x12345678);
    packet(tmp,2);
    if(may_dhcp_receive(&c,tmp,300,server)!=1 ||
       may_dhcp_request(&c,data,sizeof(data))!=300) return 15;
    packet(tmp,6);
    if(may_dhcp_receive(&c,tmp,300,server)!=-1 ||
       c.state!=MAY_DHCP_NAK) return 16;
    may_dhcp_begin(&c,mac,0x12345678);
    packet(tmp,2);
    tmp[247]=99; /* corrupted server ID */
    if(may_dhcp_receive(&c,tmp,300,server)) return 17;
    packet(tmp,2);
    tmp[244]=53; /* extra duplicate option type */
    if(may_dhcp_receive(&c,tmp,300,server)) return 18;
    packet(tmp,2);
    tmp[241]=255; /* invalid DHCP option length */
    if(may_dhcp_receive(&c,tmp,300,server)) return 19;
    return 0;
}

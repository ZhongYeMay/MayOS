#include "net_dhcp.h"

/* All lengths are explicitly bounded; BOOTP is a network protocol carrying
 * untrusted input. No dynamic allocation and no implicit packet terminators. */
static void be32(u8 *p, u32 v) {
    p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v;
}
static u32 get32(const u8 *p) {
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static int same(const u8 *a,const u8 *b,u32 length) {
    for (u32 i=0;i<length;i++) if (a[i]!=b[i]) return 0;
    return 1;
}
static int address_valid(const u8 *ip) {
    return (ip[0] != 0u && ip[0] != 127u && ip[0] < 224u &&
            !(ip[0]==169u && ip[1]==254u));
}
void may_dhcp_begin(MayDhcpClient *c,const u8 mac[6],u32 xid) {
    if (!c || !mac) return;
    memset(c,0,sizeof(*c));
    memcpy(c->mac,mac,6);
    c->xid=xid ? xid : 0x4d41594fu;
    c->state=MAY_DHCP_DISCOVER;
}
static int packet_common(const MayDhcpClient *c,u8 *p,u32 capacity,u8 type) {
    if (!c || !p || capacity < 300u || !c->xid) return 0;
    memset(p,0,300u);
    p[0]=1; p[1]=1; p[2]=6;     /* BOOTREQUEST / Ethernet MAC */
    be32(p+4,c->xid);
    p[10]=0x80; p[11]=0;       /* ask for broadcast replies */
    memcpy(p+28,c->mac,6);
    p[236]=99; p[237]=130; p[238]=83; p[239]=99;
    p[240]=53; p[241]=1; p[242]=type;
    return 243;
}
int may_dhcp_discover(const MayDhcpClient *c,u8 *p,u32 capacity) {
    if (!c || c->state!=MAY_DHCP_DISCOVER) return 0;
    int n=packet_common(c,p,capacity,1);
    if (!n) return 0;
    p[n++]=55; p[n++]=7;
    p[n++]=1; p[n++]=3; p[n++]=6; p[n++]=51;
    p[n++]=54; p[n++]=58; p[n++]=59;
    p[n++]=61; p[n++]=7; p[n++]=1;
    memcpy(p+n,c->mac,6); n+=6;
    p[n++]=255;
    return 300;
}
int may_dhcp_request(MayDhcpClient *c,u8 *p,u32 capacity) {
    if (!c || (c->state!=MAY_DHCP_OFFER && c->state!=MAY_DHCP_REQUEST) ||
        !address_valid(c->offered_ip) || !address_valid(c->server))
        return 0;
    int n=packet_common(c,p,capacity,3);
    if (!n) return 0;
    p[n++]=50; p[n++]=4;
    memcpy(p+n,c->offered_ip,4); n+=4;
    p[n++]=54; p[n++]=4;
    memcpy(p+n,c->server,4); n+=4;
    p[n++]=55; p[n++]=6;
    p[n++]=1; p[n++]=3; p[n++]=6;
    p[n++]=51; p[n++]=58; p[n++]=59;
    p[n++]=61; p[n++]=7; p[n++]=1;
    memcpy(p+n,c->mac,6); n+=6;
    p[n++]=255;
    c->state=MAY_DHCP_REQUEST;
    return 300;
}
int may_dhcp_receive(MayDhcpClient *c,const u8 *p,u32 size,
                     const u8 source_ip[4]) {
    if (!c || !p || !source_ip || size < 244u || size > 1500u ||
        (c->state!=MAY_DHCP_DISCOVER && c->state!=MAY_DHCP_REQUEST) ||
        p[0]!=2 || p[1]!=1 || p[2]!=6 ||
        get32(p+4)!=c->xid ||
        !same(p+28,c->mac,6) ||
        p[236]!=99 || p[237]!=130 || p[238]!=83 || p[239]!=99)
        return 0;
    u8 type=0,offered[4],server[4]={0},mask[4]={0},router[4]={0},
        dns[4]={0};
    u32 lease=0;
    memcpy(offered,p+16,4);
    u32 seen=0;
    for (u32 i=240u;i<size;) {
        u8 option=p[i++];
        if (option==0) continue;
        if (option==255) break;
        if (i>=size) return 0;
        u32 length=p[i++];
        if (length>size-i) return 0;
        u32 flag=0;
        if (option==53) {
            flag=1u;
            if (length!=1u) return 0;
            type=p[i];
        } else if (option==54) {
            flag=2u;
            if (length!=4u) return 0;
            memcpy(server,p+i,4);
        } else if (option==1) {
            flag=4u;
            if (length!=4u) return 0;
            memcpy(mask,p+i,4);
        } else if (option==3) {
            flag=8u;
            if (!length || (length&3u)) return 0;
            memcpy(router,p+i,4);
        } else if (option==6) {
            flag=16u;
            if (!length || (length&3u)) return 0;
            memcpy(dns,p+i,4);
        } else if (option==51) {
            flag=32u;
            if (length!=4u) return 0;
            lease=get32(p+i);
        }
        if (flag && (seen&flag)) return 0;
        seen|=flag;
        i+=length;
    }
    if (!(seen&1u)) return 0;
    if (type==6u && c->state==MAY_DHCP_REQUEST) {
        if (!(seen&2u) || !same(server,c->server,4)) return 0;
        c->state=MAY_DHCP_NAK;
        return -1;
    }
    /* QEMU/SLIRP often responds from 10.0.2.2 (server identifier).
     * Require the replying UDP peer to match the offered server identifier. */
    if (!(seen&2u) || !address_valid(server) ||
        !same(source_ip,server,4) || !address_valid(offered))
        return 0;
    if (c->state==MAY_DHCP_DISCOVER && type==2u) {
        memcpy(c->offered_ip,offered,4);
        memcpy(c->server,server,4);
        c->state=MAY_DHCP_OFFER;
        return 1;
    }
    if (c->state==MAY_DHCP_REQUEST && type==5u &&
        same(server,c->server,4) &&
        same(offered,c->offered_ip,4) &&
        (seen&32u) && lease >= 60u) {
        memcpy(c->address,offered,4);
        memcpy(c->subnet,mask,4);
        memcpy(c->router,router,4);
        memcpy(c->dns,dns,4);
        c->lease_seconds=lease;
        c->state=MAY_DHCP_BOUND;
        return 2;
    }
    return 0;
}

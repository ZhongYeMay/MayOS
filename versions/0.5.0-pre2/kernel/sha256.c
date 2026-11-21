#include "sha256.h"
static const u32 k[64]={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static u32 rr(u32 x,u32 n) {return (x>>n)|(x<<(32u-n));}
static u32 be32(const u8 *p) {
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void put32(u8 *p,u32 n) {
    p[0]=(u8)(n>>24);p[1]=(u8)(n>>16);p[2]=(u8)(n>>8);p[3]=(u8)n;
}
static void block(MaySha256 *c,const u8 p[64]) {
    u32 w[64];
    for(u32 i=0;i<16;i++)w[i]=be32(p+i*4u);
    for(u32 i=16;i<64;i++) {
        u32 a=w[i-15u],b=w[i-2u];
        u32 s0=rr(a,7)^rr(a,18)^(a>>3);
        u32 s1=rr(b,17)^rr(b,19)^(b>>10);
        w[i]=w[i-16u]+s0+w[i-7u]+s1;
    }
    u32 a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],
        f=c->h[5],g=c->h[6],h=c->h[7],cc=c->h[2];
    for(u32 i=0;i<64;i++) {
        u32 s1=rr(e,6)^rr(e,11)^rr(e,25);
        u32 choose=(e&f)^(~e&g);
        u32 t1=h+s1+choose+k[i]+w[i];
        u32 s0=rr(a,2)^rr(a,13)^rr(a,22);
        u32 majority=(a&b)^(a&cc)^(b&cc);
        u32 t2=s0+majority;
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;
    c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
void may_sha256_init(MaySha256 *c) {
    memset(c,0,sizeof(*c));
    static const u32 initial[8]={
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    for(u32 i=0;i<8;i++)c->h[i]=initial[i];
}
void may_sha256_update(MaySha256 *c,const u8 *data,u32 length) {
    if(!c || (!data&&length))return;
    c->byte_count+=(u64)length;
    for(u32 i=0;i<length;i++) {
        c->block[c->used++]=data[i];
        if(c->used==64u) {block(c,c->block);c->used=0;}
    }
}
void may_sha256_final(MaySha256 *c,u8 digest[32]) {
    if(!c || !digest)return;
    u64 bits=c->byte_count*8u;
    c->block[c->used++]=0x80u;
    if(c->used>56u) {
        while(c->used<64u)c->block[c->used++]=0;
        block(c,c->block);
        c->used=0;
    }
    while(c->used<56u)c->block[c->used++]=0;
    for(u32 i=0;i<8;i++)
        c->block[56u+i]=(u8)(bits>>((7u-i)*8u));
    block(c,c->block);
    for(u32 i=0;i<8;i++)put32(digest+i*4u,c->h[i]);
    memset(c,0,sizeof(*c));
}
void may_sha256_hex(const u8 digest[32],char hex[65]) {
    static const char digits[]="0123456789abcdef";
    for(u32 i=0;i<32;i++) {
        hex[i*2u]=digits[digest[i]>>4];
        hex[i*2u+1u]=digits[digest[i]&15u];
    }
    hex[64]=0;
}

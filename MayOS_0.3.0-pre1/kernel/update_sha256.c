#include "update_sha256.h"

#define ROR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROR((x), 2u) ^ ROR((x), 13u) ^ ROR((x), 22u))
#define EP1(x) (ROR((x), 6u) ^ ROR((x), 11u) ^ ROR((x), 25u))
#define SIG0(x) (ROR((x), 7u) ^ ROR((x), 18u) ^ ((x) >> 3u))
#define SIG1(x) (ROR((x), 17u) ^ ROR((x), 19u) ^ ((x) >> 10u))
static const unsigned int k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static unsigned int read_be32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}
static void store_be32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}
static void compress(MayUpdateSha256 *ctx) {
    unsigned int w[64];
    for (int i = 0; i < 16; ++i) w[i] = read_be32(ctx->block + i * 4);
    for (int i = 16; i < 64; ++i)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];
    unsigned int a=ctx->state[0],b=ctx->state[1],c=ctx->state[2],d=ctx->state[3];
    unsigned int e=ctx->state[4],f=ctx->state[5],g=ctx->state[6],h=ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        unsigned int t1 = h + EP1(e) + CH(e,f,g) + k[i] + w[i];
        unsigned int t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}
void may_update_sha256_init(MayUpdateSha256 *ctx) {
    static const unsigned int initial[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    for (int i=0;i<8;++i) ctx->state[i]=initial[i];
    for (int i=0;i<64;++i) ctx->block[i]=0;
    ctx->used=0;
    ctx->bytes=0;
}
void may_update_sha256_write(MayUpdateSha256 *ctx,const void *data,unsigned int size) {
    const unsigned char *p=data;
    if (!ctx || (!p && size)) return;
    ctx->bytes += (unsigned long long)size;
    for (unsigned int i=0;i<size;++i) {
        ctx->block[ctx->used++]=p[i];
        if (ctx->used==64) { compress(ctx); ctx->used=0; }
    }
}
void may_update_sha256_hex(MayUpdateSha256 *ctx,char output[65]) {
    static const char hex[]="0123456789abcdef";
    unsigned long long bit_count = ctx->bytes * 8u;
    ctx->block[ctx->used++]=0x80u;
    if (ctx->used>56) {
        while (ctx->used<64) ctx->block[ctx->used++]=0;
        compress(ctx);
        ctx->used=0;
    }
    while(ctx->used<56) ctx->block[ctx->used++]=0;
    for(int i=7;i>=0;--i) ctx->block[ctx->used++]=(unsigned char)(bit_count>>(8*i));
    compress(ctx);
    ctx->used=0;
    for(int i=0;i<8;++i) {
        unsigned char digest[4];
        store_be32(digest,ctx->state[i]);
        for(int j=0;j<4;++j) {
            output[i*8+j*2]=hex[digest[j]>>4];
            output[i*8+j*2+1]=hex[digest[j]&15];
        }
    }
    output[64]=0;
}

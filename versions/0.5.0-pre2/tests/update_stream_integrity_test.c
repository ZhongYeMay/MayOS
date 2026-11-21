#include "update_stream_integrity.h"
/* Host tests model the Rust budget ABI. Actual Rust+C linking and SHA-256
 * are tested on an x86_64 QEMU boot in the same CI workflow. */
void *memset(void *out,int value,u32 length) {
    u8 *p=out;for(u32 i=0;i<length;i++)p[i]=(u8)value;return out;
}
void *memcpy(void *out,const void *in,u32 length) {
    u8 *p=out;const u8 *s=in;
    for(u32 i=0;i<length;i++)p[i]=s[i];
    return out;
}
int mayrs_http_body_begin(const MayHttpHeaders *h,u32 bytes,MayHttpBodyBudget *b) {
    if(!h||!b||h->status!=200||h->header_bytes<16u||
       h->header_bytes>2048u||h->redirect_length||
       bytes==0||bytes>524288u||h->content_length!=bytes)return 0;
    b->declared=bytes;b->received=0;b->phase=1;b->reserved=0;
    return 1;
}
int mayrs_http_body_consume(MayHttpBodyBudget *b,u32 length) {
    if(!b)return -1;
    if(b->phase!=1||b->reserved||!length||length>4096u||
       b->received>=b->declared||length>b->declared-b->received) {
        b->phase=4;return -1;
    }
    b->received+=length;
    if(b->received==b->declared){b->phase=2;return 2;}
    return 1;
}
int mayrs_http_body_finish(MayHttpBodyBudget *b) {
    if(!b)return -1;
    if(b->phase==2&&b->received==b->declared&&b->reserved==0){
        b->phase=3;return 1;
    }
    b->phase=4;return -1;
}
static void headers(MayHttpHeaders *h,u32 bytes) {
    memset(h,0,sizeof(*h));
    h->status=200;h->header_bytes=42;h->content_length=bytes;
}
int main(void){
    /* SHA-256 of 8192 ASCII 'Z' bytes, independently computed. */
    static const u8 hash_8192[32]={
        0x1a,0xe6,0x2b,0x31,0x10,0x14,0x1b,0xf4,
        0x3a,0xf6,0xa7,0xa1,0x48,0x75,0x44,0x2a,
        0xfa,0xea,0x84,0x60,0x12,0x2b,0x81,0x4e,
        0x36,0x46,0x6f,0xeb,0xf3,0x9c,0xa6,0x54
    };
    u8 chunk[4096];
    memset(chunk,'Z',sizeof(chunk));
    MayHttpHeaders h;
    MayUpdateStreamIntegrity ctx;
    if(!may_update_stream_selftest())return 1;
    headers(&h,8192);
    if(!may_update_stream_begin(&ctx,&h,8192,hash_8192))return 2;
    if(may_update_stream_feed(&ctx,chunk,4096)!=1)return 3;
    if(may_update_stream_feed(&ctx,chunk,4096)!=2)return 4;
    if(!may_update_stream_finish(&ctx))return 5;
    if(may_update_stream_feed(&ctx,chunk,1)!=0)return 6;
    if(!may_update_stream_begin(&ctx,&h,8192,hash_8192))return 7;
    chunk[100]^=1;
    if(may_update_stream_feed(&ctx,chunk,4096)!=1)return 8;
    chunk[100]^=1;
    if(may_update_stream_feed(&ctx,chunk,4096)!=2)return 9;
    if(may_update_stream_finish(&ctx)!=0)return 10;
    if(!may_update_stream_begin(&ctx,&h,8192,hash_8192))return 11;
    if(may_update_stream_feed(&ctx,chunk,4096)!=1)return 12;
    if(may_update_stream_finish(&ctx)!=0)return 13;
    if(!may_update_stream_begin(&ctx,&h,8192,hash_8192))return 14;
    if(may_update_stream_feed(&ctx,chunk,4097)!=0)return 15;
    if(may_update_stream_feed(&ctx,chunk,1)!=0)return 16;
    if(!may_update_stream_begin(&ctx,&h,8192,hash_8192))return 17;
    if(may_update_stream_feed(&ctx,chunk,0)!=0)return 18;
    h.content_length=8191;
    if(may_update_stream_begin(&ctx,&h,8192,hash_8192)!=0)return 19;
    headers(&h,524289);
    if(may_update_stream_begin(&ctx,&h,524289,hash_8192)!=0)return 20;
    if(may_update_stream_begin((MayUpdateStreamIntegrity*)0,&h,3,hash_8192)!=0)return 21;
    headers(&h,8192);
    if(may_update_stream_begin(&ctx,&h,8192,(const u8*)0)!=0)return 22;
    return 0;
}

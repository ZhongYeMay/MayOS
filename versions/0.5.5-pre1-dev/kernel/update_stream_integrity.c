#include "update_stream_integrity.h"
#ifdef MAYOS_RUST_CORE
enum { STREAM_ACTIVE=1u, STREAM_VERIFIED=2u, STREAM_FAILED=3u };
int may_update_stream_begin(MayUpdateStreamIntegrity *ctx,
                            const MayHttpHeaders *headers,u32 signed_bytes,
                            const u8 expected_sha256[32]) {
    if(!ctx)return 0;
    memset(ctx,0,sizeof(*ctx));
    ctx->state=STREAM_FAILED;
    if(!headers||!expected_sha256)return 0;
    /* Rust checks HTTP 200, header bounds, zero redirects, exact signed
     * Content-Length, and a maximum body size of 512KiB. */
    if(mayrs_http_body_begin(headers,signed_bytes,&ctx->budget)!=1)return 0;
    memcpy(ctx->expected_sha256,expected_sha256,32u);
    may_sha256_init(&ctx->sha);
    ctx->state=STREAM_ACTIVE;
    return 1;
}
int may_update_stream_feed(MayUpdateStreamIntegrity *ctx,
                           const u8 *bytes,u32 length) {
    if(!ctx)return 0;
    if(ctx->state!=STREAM_ACTIVE||!bytes||!length||length>4096u) {
        ctx->state=STREAM_FAILED;
        return 0;
    }
    /* Rust consumes the byte budget BEFORE any SHA state mutation. */
    int result=mayrs_http_body_consume(&ctx->budget,length);
    if(result!=1&&result!=2) {
        ctx->state=STREAM_FAILED;
        return 0;
    }
    may_sha256_update(&ctx->sha,bytes,length);
    return result;
}
int may_update_stream_finish(MayUpdateStreamIntegrity *ctx) {
    if(!ctx)return 0;
    if(ctx->state!=STREAM_ACTIVE||
       mayrs_http_body_finish(&ctx->budget)!=1) {
        ctx->state=STREAM_FAILED;
        return 0;
    }
    u8 actual[32];
    may_sha256_final(&ctx->sha,actual);
    /* Examine every digest byte, even for an early mismatch. */
    u8 mismatch=0;
    for(u32 i=0;i<32u;i++)mismatch|=(u8)(actual[i]^ctx->expected_sha256[i]);
    memset(actual,0,sizeof(actual));
    memset(ctx->expected_sha256,0,sizeof(ctx->expected_sha256));
    ctx->state=mismatch?STREAM_FAILED:STREAM_VERIFIED;
    return mismatch==0;
}
int may_update_stream_selftest(void) {
    static const u8 known_sha[32]={
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const u8 good[3]={'a','b','c'};
    static const u8 bad[3]={'a','b','d'};
    MayHttpHeaders h;
    MayUpdateStreamIntegrity ctx;
    memset(&h,0,sizeof(h));
    h.status=200;h.header_bytes=42;h.content_length=3;
    if(!may_update_stream_begin(&ctx,&h,3,known_sha)||
       may_update_stream_feed(&ctx,good,2)!=1||
       may_update_stream_feed(&ctx,good+2,1)!=2||
       !may_update_stream_finish(&ctx)||
       may_update_stream_finish(&ctx)!=0)return 0;
    if(!may_update_stream_begin(&ctx,&h,3,known_sha)||
       may_update_stream_feed(&ctx,bad,3)!=2||
       may_update_stream_finish(&ctx)!=0)return 0;
    if(!may_update_stream_begin(&ctx,&h,3,known_sha)||
       may_update_stream_feed(&ctx,good,2)!=1||
       may_update_stream_finish(&ctx)!=0)return 0;
    if(!may_update_stream_begin(&ctx,&h,3,known_sha)||
       may_update_stream_feed(&ctx,good,3)!=2||
       may_update_stream_feed(&ctx,good,1)!=0||
       may_update_stream_finish(&ctx)!=0)return 0;
    /* Actual Rust+C native QEMU test over a 64KiB image-sized stream.
     * Expected SHA-256 of exactly 65536 'Z' bytes is independently fixed. */
    static const u8 large_sha[32]={
        0x94,0x40,0x44,0xfe,0x48,0x2b,0xc4,0xe9,
        0x10,0x85,0xc1,0x5c,0x5a,0x92,0x3a,0x1b,
        0x9e,0x02,0xea,0xc9,0x8d,0x3b,0xce,0x04,
        0x99,0x7d,0x6d,0xbe,0xcd,0x2a,0x5b,0x8d
    };
    static u8 chunk[4096];
    memset(chunk,'Z',sizeof(chunk));
    h.content_length=65536;
    if(!may_update_stream_begin(&ctx,&h,65536,large_sha))return 0;
    for(u32 i=0;i<16u;i++){
        int status=may_update_stream_feed(&ctx,chunk,sizeof(chunk));
        if(status!=(i==15u?2:1))return 0;
    }
    if(!may_update_stream_finish(&ctx))return 0;
    return 1;
}
#endif

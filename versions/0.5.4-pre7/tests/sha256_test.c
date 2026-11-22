#include "sha256.h"
/* SHA-256 FIPS vectors plus multi-block and single-byte incremental streams. */
void *memset(void *dest,int value,u32 length) {
    u8 *p=dest;
    for(u32 i=0;i<length;i++)p[i]=(u8)value;
    return dest;
}
static int same(const char *a,const char *b) {
    while(*a && *a==*b){a++;b++;}
    return *a==*b;
}
static int check(const u8 *bytes,u32 n,const char *expected,int split) {
    MaySha256 ctx;
    u8 digest[32];
    char hex[65];
    may_sha256_init(&ctx);
    if(split) {
        for(u32 i=0;i<n;i++)may_sha256_update(&ctx,bytes+i,1u);
    } else may_sha256_update(&ctx,bytes,n);
    may_sha256_final(&ctx,digest);
    may_sha256_hex(digest,hex);
    return same(hex,expected);
}
int main(void) {
    static const char empty[]="";
    static const char abc[]="abc";
    u8 data[1000];
    memset(data,'a',sizeof(data));
    if(!check((const u8 *)empty,0,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",0))
        return 1;
    if(!check((const u8 *)abc,3,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",0))
        return 2;
    if(!check(data,64,
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",0))
        return 3;
    if(!check(data,1000,
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",0))
        return 4;
    if(!check(data,1000,
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",1))
        return 5;
    return 0;
}

#include "core.h"
/* BearSSL's portable C routines can refer to these ISO C operations. These
 * functions are only present in an explicitly opted-in TLS engine build.
 * The default MayKernel build is unchanged. */
#ifdef MAYOS_USE_BEARSSL
int memcmp(const void *left,const void *right,unsigned long length) {
    const u8 *a=left,*b=right;
    for(unsigned long i=0;i<length;i++)
        if(a[i]!=b[i])return a[i]<b[i]?-1:1;
    return 0;
}
void *memmove(void *destination,const void *source,unsigned long length) {
    u8 *dst=destination;
    const u8 *src=source;
    if(dst==src||!length)return destination;
    if((uptr)dst>(uptr)src && (uptr)dst<(uptr)src+length) {
        while(length) {length--;dst[length]=src[length];}
    } else {
        for(unsigned long i=0;i<length;i++)dst[i]=src[i];
    }
    return destination;
}
#endif

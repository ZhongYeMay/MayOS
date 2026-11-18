#include <stddef.h>

/*
 * Explicit ISO C sized compatibility shims for the optional BearSSL build.
 * BearSSL source is compiled with -Dmemcpy=maybear_memcpy (etc), so it NEVER
 * calls MayKernel's older 32-bit-length memcpy/memset/strlen ABI.
 * No heap, libc or system calls. Security code remains fail-closed.
 */
#ifdef MAYOS_USE_BEARSSL
#include "bearssl.h"
/*
 * Refuse host OS seeding: native CSPRNG is not yet available.
 * BearSSL may inspect this callback, but cannot silently use Linux syscalls
 * or obtain deterministic "random" bytes. HTTPS stays hard-disabled.
 */
br_prng_seeder br_prng_seeder_system(const char **name) {
    if(name)*name="no MayKernel cryptographic entropy";
    return 0;
}
void *maybear_memcpy(void *dest,const void *source,size_t n) {
    unsigned char *d=dest;
    const unsigned char *s=source;
    for(size_t i=0;i<n;i++)d[i]=s[i];
    return dest;
}
void *maybear_memset(void *dest,int value,size_t n) {
    unsigned char *d=dest;
    for(size_t i=0;i<n;i++)d[i]=(unsigned char)value;
    return dest;
}
void *maybear_memmove(void *dest,const void *source,size_t n) {
    unsigned char *d=dest;
    const unsigned char *s=source;
    if(d==s||!n)return dest;
    if((size_t)d>(size_t)s && (size_t)d-(size_t)s<n) {
        while(n){n--;d[n]=s[n];}
    } else {
        for(size_t i=0;i<n;i++)d[i]=s[i];
    }
    return dest;
}
/* Some upstream files call standard memmove without including string.h,
 * so the -Dmemmove alias is not guaranteed to cover every call site. */
void *memmove(void *dest,const void *source,size_t n) {
    return maybear_memmove(dest,source,n);
}
int maybear_memcmp(const void *a,const void *b,size_t n) {
    const unsigned char *x=a,*y=b;
    for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;
    return 0;
}
size_t maybear_strlen(const char *s) {
    size_t n=0;
    while(s[n])n++;
    return n;
}
#endif

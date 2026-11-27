#include <stddef.h>

/*
 * Explicit ISO C sized compatibility shims for the optional BearSSL build.
 * BearSSL source is compiled with -Dmemcpy=maybear_memcpy (etc), so it NEVER
 * calls MayKernel's older 32-bit-length memcpy/memset/strlen ABI.
 * No heap, libc or system calls. Security code remains fail-closed.
 */
#ifdef MAYOS_USE_BEARSSL
#include "bearssl.h"
/* Kernel byte API shared with the freestanding entropy sampler. */
#include "security_entropy_seed.h"

static void wipe(void *buffer, size_t length) {
    volatile unsigned char *bytes = buffer;
    while (length--) *bytes++ = 0;
}

static int seed_from_cpu(const br_prng_class **context) {
    unsigned char seed[MAY_ENTROPY_SEED_BYTES];
    if (!may_entropy_seed(seed)) return 0;
    (*context)->update(context, seed, sizeof(seed));
    wipe(seed, sizeof(seed));
    return 1;
}

br_prng_seeder br_prng_seeder_system(const char **name) {
    if (name) *name = "MayKernel CPU RDSEED";
    return seed_from_cpu;
}

int may_tls_provider_seed_ready(void) {
    br_hmac_drbg_context context;
    br_hmac_drbg_init(&context, &br_sha256_vtable, NULL, 0);
    int ready = seed_from_cpu(&context.vtable);
    wipe(&context, sizeof(context));
    return ready;
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

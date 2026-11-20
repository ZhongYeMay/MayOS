#include "security_tls_provider.h"
#ifdef MAYOS_USE_BEARSSL
#include "bearssl.h"
/* This translation unit uses BearSSL's standard-C ABI, not the kernel's
 * legacy u32-sized memory function declarations. */
extern void serial(const char *message);

/*
 * Statically allocated to avoid heap/allocator assumptions. We intentionally
 * install NO trust anchors and NO system time and never call client_reset().
 * br_ssl_client_init_full is only a link/initialization proof, not a TLS
 * security assertion. Disabling the network path is required.
 */
static br_ssl_client_context client;
static br_x509_minimal_context x509;
static int linked;
int may_tls_provider_probe(void) {
    if(linked)return 1;
    /* Static BSS contexts are initially zeroed at kernel bootstrap. */
    br_ssl_client_init_full(&client,&x509,0,0);
    br_ssl_engine_set_versions(&client.eng,BR_TLS12,BR_TLS12);
    /* x86_64 CPUID exposes SSE2 even when MayKernel has not enabled the
     * XMM/FPU save/restore state. BearSSL's default CPU-based dispatcher
     * selected br_chacha20_sse2_run and faulted with #UD in real QEMU.
     * Pin a single modern AEAD suite with the portable constant-time C
     * implementations until kernel XMM state handling is fully supported. */
    static const uint16_t safe_suite[]={
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
    };
    br_ssl_engine_set_suites(&client.eng,safe_suite,1);
    br_ssl_engine_set_chacha20(&client.eng,&br_chacha20_ct_run);
    br_ssl_engine_set_poly1305(&client.eng,&br_poly1305_ctmul_run);
    linked=1;
    serial("MayOS: BearSSL scalar-only AEAD backend selected (NO TRUST/NO HTTPS)\n");
    serial("MayOS: BearSSL TLS engine structures initialized (NO TRUST/NO HANDSHAKE)\n");
    return 1;
}
int may_tls_provider_is_linked(void) {return linked;}
int may_tls_provider_scalar_only(void) {
    return linked && client.eng.ichacha==&br_chacha20_ct_run &&
           client.eng.ipoly==&br_poly1305_ctmul_run;
}
#else
int may_tls_provider_probe(void) {return 0;}
int may_tls_provider_is_linked(void) {return 0;}
int may_tls_provider_scalar_only(void) {return 0;}
#endif

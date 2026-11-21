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
    linked=1;
    serial("MayOS: BearSSL TLS engine structures initialized (NO TRUST/NO HANDSHAKE)\n");
    return 1;
}
int may_tls_provider_is_linked(void) {return linked;}
#else
int may_tls_provider_probe(void) {return 0;}
int may_tls_provider_is_linked(void) {return 0;}
#endif

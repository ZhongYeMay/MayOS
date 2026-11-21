/*
 * TEST-ONLY BearSSL X.509 chain validation in native x86_64 MayKernel.
 * Certificate, RSA CA trust anchor and independent test time are generated
 * by CI from disposable OpenSSL test certificates; no private key is linked.
 *
 * This tests BearSSL's REAL X.509 engine and deliberately performs NO TLS
 * handshake, no TCP connection and NO production CA trust initialization.
 * Never set may_tls_security_report().x509_engine_ready from this test.
 *
 * IMPORTANT: do not include core.h here. BearSSL has a native size_t libc
 * ABI; legacy MayKernel core.h declares memcpy/memset with u32 size.
 */
#ifdef MAYOS_TLS_X509_DIAGNOSTIC
#include "bearssl.h"
#include "tls_x509_fixture.h"
extern void serial(const char *message);

static br_x509_minimal_context x509;
static br_x509_trust_anchor anchor;

static void set_test_anchor(void) {
    anchor.dn.data=mayos_test_ca_dn;
    anchor.dn.len=sizeof(mayos_test_ca_dn);
    anchor.flags=BR_X509_TA_CA;
    anchor.pkey.key_type=BR_KEYTYPE_RSA;
    anchor.pkey.key.rsa.n=mayos_test_ca_rsa_modulus;
    anchor.pkey.key.rsa.nlen=sizeof(mayos_test_ca_rsa_modulus);
    anchor.pkey.key.rsa.e=mayos_test_ca_rsa_exponent;
    anchor.pkey.key.rsa.elen=sizeof(mayos_test_ca_rsa_exponent);
}
static unsigned check_cert(const char *host,unsigned delta_days,int trust_ca) {
    br_x509_minimal_init_full(&x509,trust_ca?&anchor:0,trust_ca?1:0);
    br_x509_minimal_set_time(&x509,
        MAYOS_TLS_FIXTURE_DAYS+delta_days,MAYOS_TLS_FIXTURE_SECONDS);
    x509.vtable->start_chain(&x509.vtable,host);
    x509.vtable->start_cert(&x509.vtable,
                           (unsigned)sizeof(mayos_test_leaf_der));
    /* Chunked DER feed tests the same state transitions as a TLS record
     * reader would invoke after future verified TLS transport exists. */
    size_t offset=0;
    while(offset<sizeof(mayos_test_leaf_der)) {
        size_t n=sizeof(mayos_test_leaf_der)-offset;
        if(n>137)n=137;
        x509.vtable->append(&x509.vtable,mayos_test_leaf_der+offset,n);
        offset+=n;
    }
    x509.vtable->end_cert(&x509.vtable);
    return x509.vtable->end_chain(&x509.vtable);
}
int may_tls_x509_fixture_selftest(void) {
    set_test_anchor();
    unsigned accepted=check_cert("test.mayos.local",0,1);
    unsigned bad_host=check_cert("incorrect.mayos.local",0,1);
    unsigned bad_time=check_cert("test.mayos.local",40,1);
    unsigned bad_ca=check_cert("test.mayos.local",0,0);
    if(accepted==0 && bad_host!=0 && bad_time!=0 && bad_ca!=0) {
        serial("MayOS: BearSSL X.509 CA, hostname and validity diagnostics PASS (NO HANDSHAKE)\n");
        return 1;
    }
    serial("MayOS: BearSSL X.509 diagnostic FAILED (NO HANDSHAKE)\n");
    return 0;
}
#endif

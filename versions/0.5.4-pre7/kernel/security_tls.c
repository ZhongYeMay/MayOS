#include "security_tls.h"
#include "security_tls_provider.h"
#include "security_trusted_time.h"

static MayTlsSecurityReport security;
int may_tls_update_host_allowed(const char *name) {
    if(!name || strlen(name)>253u)return 0;
    /* Exact host names; no userinfo, ports, suffix tricks or mixed URLs.
     * Redirects require their own authenticated hostname checks later. */
    return !strcmp(name,"api.github.com") || !strcmp(name,"github.com");
}
void may_tls_security_init(void) {
    memset(&security,0,sizeof(security));
    int entropy=may_entropy_probe_hardware();
    MayRtcObservation rtc;
    security.hardware_entropy_observed=
        entropy==MAY_ENTROPY_HW_SAMPLED_UNTRUSTED;
    security.rtc_observed=may_rtc_read_observed(&rtc);
    security.cryptographic_drbg_ready=may_tls_provider_seed_ready();
    security.trusted_time_ready=0;
    security.certificate_roots_ready=may_tls_provider_roots_ready();
    security.x509_engine_ready=0;
    security.authenticated_handshake=0;
    /* Optional TLS engine object compilation does not establish trust. */
    security.x509_engine_ready=may_tls_provider_probe();
    serial(security.hardware_entropy_observed
        ? "MayOS: CPU entropy instruction sampled (NOT a TLS CSPRNG)\n"
        : "MayOS: no validated CPU entropy source for TLS\n");
    serial(security.rtc_observed
        ? "MayOS: CMOS date observed (UNTRUSTED TLS time)\n"
        : "MayOS: RTC reading unavailable (TLS time untrusted)\n");
    serial(security.cryptographic_drbg_ready
        ? "MayOS: BearSSL HMAC-DRBG seeded from CPU RDSEED\n"
        : "MayOS: BearSSL HMAC-DRBG unavailable (RDSEED or provider)\n");
    serial("MayOS: TLS blocked: authenticated time has not been provisioned\n");
}
const MayTlsSecurityReport *may_tls_security_report(void) {
    unsigned days, seconds;
    security.trusted_time_ready=may_time_read(&days,&seconds);
    return &security;
}
int may_tls_update_preflight(const char *hostname) {
    (void)may_tls_security_report();
    if(!may_tls_update_host_allowed(hostname))return MAY_TLS_BLOCK_HOST;
    if(!security.cryptographic_drbg_ready)return MAY_TLS_BLOCK_ENTROPY;
    if(!security.trusted_time_ready)return MAY_TLS_BLOCK_TRUSTED_TIME;
    if(!security.certificate_roots_ready)return MAY_TLS_BLOCK_CERT_STORE;
    if(!security.x509_engine_ready)return MAY_TLS_BLOCK_ENGINE;
    if(!security.authenticated_handshake)return MAY_TLS_BLOCK_HANDSHAKE;
    /* Defensive hard block. Do not let future callers bypass a missing
     * verified provider by setting report flags. */
    return MAY_TLS_BLOCK_ENGINE;
}
const char *may_tls_block_reason(int r) {
    switch(r) {
        case MAY_TLS_BLOCK_HOST:
            return "TLS BLOCKED: update host must be api.github.com or github.com";
        case MAY_TLS_BLOCK_ENTROPY:
            return "TLS BLOCKED: cryptographic DRBG is unavailable";
        case MAY_TLS_BLOCK_TRUSTED_TIME:
            return "TLS BLOCKED: trusted certificate-validation time unavailable";
        case MAY_TLS_BLOCK_CERT_STORE:
            return "TLS BLOCKED: trusted CA roots are not installed";
        case MAY_TLS_BLOCK_ENGINE:
            return "TLS BLOCKED: verified X.509/TLS provider not linked";
        default:
            return "TLS BLOCKED: server certificate handshake not authenticated";
    }
}
int may_tls_open_verified_update(const char *hostname) {
    (void)may_tls_update_preflight(hostname);
    /* NO network request or filesystem modification. Fail closed. */
    return 0;
}

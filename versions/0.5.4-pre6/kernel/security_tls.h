#ifndef MAYOS_SECURITY_TLS_H
#define MAYOS_SECURITY_TLS_H
#include "core.h"
#include "security_clock.h"
#include "security_entropy.h"

/* Policy checkpoint, NOT a TLS protocol implementation. This translation
 * unit MUST NOT allow HTTPS until a real X.509-validating TLS library with
 * a CSPRNG, trusted time and roots has been linked and verified in QEMU. */
enum {
    MAY_TLS_BLOCK_HOST=1,
    MAY_TLS_BLOCK_ENTROPY=2,
    MAY_TLS_BLOCK_TRUSTED_TIME=3,
    MAY_TLS_BLOCK_CERT_STORE=4,
    MAY_TLS_BLOCK_ENGINE=5,
    MAY_TLS_BLOCK_HANDSHAKE=6
};
typedef struct {
    int hardware_entropy_observed;
    int rtc_observed;
    int cryptographic_drbg_ready;
    int trusted_time_ready;
    int certificate_roots_ready;
    int x509_engine_ready;
    int authenticated_handshake;
} MayTlsSecurityReport;

void may_tls_security_init(void);
const MayTlsSecurityReport *may_tls_security_report(void);
int may_tls_update_host_allowed(const char *hostname);
int may_tls_update_preflight(const char *hostname);
const char *may_tls_block_reason(int reason);
/* Always returns 0 until a real vetted provider is integrated. */
int may_tls_open_verified_update(const char *hostname);
#endif

#ifndef MAYOS_SECURITY_ENTROPY_H
#define MAYOS_SECURITY_ENTROPY_H
#include "core.h"
#include "security_entropy_seed.h"
/* Hardware instruction diagnostic, NOT a cryptographic DRBG. A positive
 * reading does not prove an unpredictable TLS client nonce on virtual CPUs. */
enum {
    MAY_ENTROPY_NONE=0,
    MAY_ENTROPY_CPU_ABSENT=1,
    MAY_ENTROPY_CPU_FAILED=2,
    MAY_ENTROPY_HW_SAMPLED_UNTRUSTED=3
};
int may_entropy_healthcheck(const u64 words[8]);
int may_entropy_probe_hardware(void);
#endif

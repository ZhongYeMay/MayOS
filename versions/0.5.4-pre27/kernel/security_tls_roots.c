#include "security_tls_stream.h"
#include "tls_system_roots.h"

const br_x509_trust_anchor *may_tls_system_anchors(size_t *count) {
    *count = sizeof(mozilla_anchors) / sizeof(mozilla_anchors[0]);
    return mozilla_anchors;
}

int may_tls_provider_roots_ready(void) {
    return sizeof(mozilla_anchors) / sizeof(mozilla_anchors[0]) != 0;
}

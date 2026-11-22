#ifndef MAYOS_SECURITY_TLS_STREAM_H
#define MAYOS_SECURITY_TLS_STREAM_H

/* Kernel-private TLS transport. The caller owns the anchors for the session
 * and must supply authenticated wall time. Production uses CPU RDSEED.
 * Explicit seed input is reserved for the isolated diagnostic build. */
#include "bearssl.h"

enum { MAY_TLS_STREAM_IDLE, MAY_TLS_STREAM_HANDSHAKE, MAY_TLS_STREAM_READY,
       MAY_TLS_STREAM_CLOSED, MAY_TLS_STREAM_FAILED };
int may_tls_stream_open(const unsigned char address[4], unsigned short port,
                        const char *hostname, const br_x509_trust_anchor *anchors,
                        size_t count, unsigned days, unsigned seconds,
                        const unsigned char *seed, size_t seed_length);
const br_x509_trust_anchor *may_tls_system_anchors(size_t *count);
int may_tls_public_open(const unsigned char address[4], unsigned short port, const char *hostname);
void may_tls_stream_poll(void);
int may_tls_stream_state(void);
int may_tls_stream_error(void);
size_t may_tls_stream_read(unsigned char *output, size_t capacity);
size_t may_tls_stream_write(const unsigned char *data, size_t length);
void may_tls_stream_close(void);
#endif

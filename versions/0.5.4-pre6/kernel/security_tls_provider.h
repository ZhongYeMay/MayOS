#ifndef MAYOS_SECURITY_TLS_PROVIDER_H
#define MAYOS_SECURITY_TLS_PROVIDER_H
/*
 * Provider-link probe for the optional BearSSL native ELF64 build.
 * The probe initializes TLS engine structures with ZERO CA anchors and never
 * starts a handshake. It is NOT an HTTPS client and MUST NOT enable updates.
 */
int may_tls_provider_probe(void);
int may_tls_provider_is_linked(void);
int may_tls_provider_seed_ready(void);
#endif

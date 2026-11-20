#ifndef MAYOS_RUST_BRIDGE_H
#define MAYOS_RUST_BRIDGE_H
#include "core.h"
/* Rust no_std ABI version 1. A valid manifest remains untrusted metadata.
 * The MayFS caller MUST ensure data has length initialized bytes. */
#ifdef MAYOS_RUST_CORE
int mayrs_manifest_gate(const u8 *data, u32 length);
u32 mayrs_abi_version(void);
/* URL preflight is a pure parser, NEVER a TLS, DNS or HTTPS connection. */
int mayrs_release_url_allowed(const u8 *data,u32 length,u32 redirects);
int mayrs_release_url_selftest(void);
/* Read-only strict HTTP/1.1 framing check; no network or download.
 * The ABI struct layout is separately declared for future transport use. */
int mayrs_http_headers_selftest(void);
/* Offline byte-budget: does not parse TLS records, hash or install. */
int mayrs_http_body_selftest(void);
#ifdef MAYOS_AB_DISK_DIAGNOSTIC
/* Test-only journal codec, never enabled in a production installer. */
int mayrs_ab_disk_initial(u8 *out, u32 length);
u32 mayrs_ab_disk_select(const u8 *a, const u8 *b, u8 *out, u32 length);
int mayrs_ab_disk_synthetic_stage(const u8 *source, u8 *out, u32 length);
/* Strictly opt-in 8MiB QEMU test disk ATA operations; no installer. */
int may_ab_journal_diagnostic(int command);
#endif
/* In-memory A/B transition policy diagnostic only, NEVER installs/boots.
 * Can be invoked on real MayKernel without writing disks. */
int mayrs_ab_selftest(void);
#ifdef MAYOS_RELEASE_SIGNATURE
/* Detached strict Ed25519 verification. Key must be a pinned release root.
 * Returns cryptographic validity only; no permission to install. */
int mayrs_ed25519_verify(const u8 *public_key, const u8 *signature,
                        const u8 *manifest, u32 bytes);
#endif
#endif
#endif

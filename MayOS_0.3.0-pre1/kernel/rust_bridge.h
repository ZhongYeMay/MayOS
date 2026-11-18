#ifndef MAYOS_RUST_BRIDGE_H
#define MAYOS_RUST_BRIDGE_H
#include "core.h"
/* Rust no_std ABI version 1. A valid manifest remains untrusted metadata.
 * The MayFS caller MUST ensure data has length initialized bytes. */
#ifdef MAYOS_RUST_CORE
int mayrs_manifest_gate(const u8 *data, u32 length);
u32 mayrs_abi_version(void);
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

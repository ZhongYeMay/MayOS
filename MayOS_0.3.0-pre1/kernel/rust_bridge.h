#ifndef MAYOS_RUST_BRIDGE_H
#define MAYOS_RUST_BRIDGE_H
#include "core.h"
/* Rust no_std ABI version 1. A valid manifest remains untrusted metadata.
 * The MayFS caller MUST ensure data has length initialized bytes. */
#ifdef MAYOS_RUST_CORE
int mayrs_manifest_gate(const u8 *data, u32 length);
u32 mayrs_abi_version(void);
#endif
#endif

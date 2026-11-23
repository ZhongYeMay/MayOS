#ifndef MAYOS_UPDATE_STREAM_INTEGRITY_H
#define MAYOS_UPDATE_STREAM_INTEGRITY_H
#include "sha256.h"
#include "rust_bridge.h"
#ifdef MAYOS_RUST_CORE
/* Offline-only combination of the Rust byte budget and native SHA-256.
 * The expected size and digest MUST come from a separately authenticated,
 * publisher-signed manifest, never raw HTTP headers. Success is never
 * authority to install or modify the bootloader. */
typedef struct {
    MayHttpBodyBudget budget;
    MaySha256 sha;
    u8 expected_sha256[32];
    u32 state;
} MayUpdateStreamIntegrity;
int may_update_stream_begin(MayUpdateStreamIntegrity *ctx,
                            const MayHttpHeaders *headers,u32 signed_bytes,
                            const u8 expected_sha256[32]);
/* 1: continuing, 2: exact size reached, 0: permanent failure. */
int may_update_stream_feed(MayUpdateStreamIntegrity *ctx,
                           const u8 *bytes,u32 length);
/* 1: verified length AND SHA-256, 0: failed. Never performs installation. */
int may_update_stream_finish(MayUpdateStreamIntegrity *ctx);
int may_update_stream_selftest(void);
#endif
#endif

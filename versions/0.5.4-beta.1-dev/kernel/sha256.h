#ifndef MAYOS_SHA256_H
#define MAYOS_SHA256_H
#include "core.h"
/* Native freestanding SHA-256. Integrity only: it does NOT authenticate
 * a manifest or the release publisher and is not a signature verifier. */
typedef struct {
    u32 h[8];
    u8 block[64];
    u32 used;
    u64 byte_count;
} MaySha256;
void may_sha256_init(MaySha256 *ctx);
void may_sha256_update(MaySha256 *ctx,const u8 *data,u32 length);
void may_sha256_final(MaySha256 *ctx,u8 digest[32]);
void may_sha256_hex(const u8 digest[32],char hex[65]);
#endif

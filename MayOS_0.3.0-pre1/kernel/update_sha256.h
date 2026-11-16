#ifndef MAYOS_UPDATE_SHA256_H
#define MAYOS_UPDATE_SHA256_H
/* SHA-256 (FIPS 180-4), portable and freestanding: MayKernel and Linux.
 * No heap, libc, network, disk access or runtime dependencies. */
typedef struct {
    unsigned int state[8];
    unsigned char block[64];
    unsigned int used;
    unsigned long long bytes;
} MayUpdateSha256;
void may_update_sha256_init(MayUpdateSha256 *ctx);
void may_update_sha256_write(MayUpdateSha256 *ctx, const void *data, unsigned int size);
void may_update_sha256_hex(MayUpdateSha256 *ctx, char output[65]);
#endif

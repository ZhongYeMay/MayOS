#ifndef MAYOS_UPDATE_STORE_H
#define MAYOS_UPDATE_STORE_H
#include "update_bootplan.h"
/* Isolated A/B boot RECORD store only. It owns exactly two 512-byte metadata
 * sectors supplied through callbacks. It never names or accesses real disk
 * LBAs, MayFS content, kernel partitions or the current boot volume. */
#define MAY_UPDATE_STORE_BYTES 512u
typedef struct {
    int (*read)(void *user, unsigned int copy, unsigned char block[MAY_UPDATE_STORE_BYTES]);
    int (*write)(void *user, unsigned int copy, const unsigned char block[MAY_UPDATE_STORE_BYTES]);
    int (*flush)(void *user);
    void *user;
} MayUpdateStoreIO;
int may_update_store_load(const MayUpdateStoreIO *io, MayBootRecord *out);
/* Returns 1 only after the alternate copy was written, flushed and read back.
 * On failure a previously valid boot record should remain intact. */
int may_update_store_commit(const MayUpdateStoreIO *io,
                            const MayBootRecord *next);
#endif

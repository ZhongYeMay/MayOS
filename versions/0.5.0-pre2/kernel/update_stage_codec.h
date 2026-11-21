#ifndef MAYOS_UPDATE_STAGE_CODEC_H
#define MAYOS_UPDATE_STAGE_CODEC_H
#include "core.h"

/* This header describes a test-only, unsigned download staging format.
 * A "verified" SHA-256 is INTEGRITY ONLY, never official publisher trust. */
#define MAY_STAGE_SECTOR_BYTES 512u
#define MAY_STAGE_DATA_LBA 2048u
#define MAY_STAGE_DATA_SECTORS 1024u
#define MAY_STAGE_CAPACITY (MAY_STAGE_DATA_SECTORS*MAY_STAGE_SECTOR_BYTES)
#define MAY_STAGE_HEADER_A 1536u
#define MAY_STAGE_HEADER_B 1537u
#define MAY_STAGE_DOWNLOADING 1u
#define MAY_STAGE_HASH_VALID 2u
#define MAY_STAGE_CHUNK_MAX 4096u

typedef struct {
    u32 generation;
    u32 total_bytes;
    u32 stored_bytes;
    u32 phase;
    u8 digest[32];
} MayStageRecord;

/* All functions below are deterministic/side-effect-free (no disks).
 * A false result must never authorize installing the staged bytes. */
void may_stage_encode(u8 sector[512],const MayStageRecord *state);
int may_stage_decode(const u8 sector[512],MayStageRecord *state);
int may_stage_choose(const u8 a[512],const u8 b[512],
                     MayStageRecord *state,u32 *active_header_lba);
int may_stage_plan_append(const MayStageRecord *state,u32 length);
#endif

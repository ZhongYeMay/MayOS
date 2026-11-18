#ifndef MAYOS_UPDATE_H
#define MAYOS_UPDATE_H
#include "core.h"

/* This interface only examines a local, UNTRUSTED update manifest.
 * It never downloads images, flashes disks, or modifies boot entries.
 * A secure installer must authenticate and verify the complete image first. */
typedef enum {
    MAY_UPDATE_NOT_CHECKED = 0,
    MAY_UPDATE_MISSING,
    MAY_UPDATE_INVALID,
    MAY_UPDATE_CURRENT,
    MAY_UPDATE_CANDIDATE
} MayUpdateState;

typedef struct {
    MayUpdateState state;
    char name[33];
    char version[33];
    char architecture[17];
    char kernel[17];
    char release_date[11];
    char sha256[65];
    char changelog[49];
    u32 size_bytes;
    u32 eta_minutes;
} MayUpdateInfo;

/* Read Documents/Update.may from MayFS; return an unverified preview. */
void may_update_check(void);
const MayUpdateInfo *may_update_info(void);
const char *may_update_status(void);

#endif

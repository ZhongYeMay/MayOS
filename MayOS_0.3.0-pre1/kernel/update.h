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
/* Integrity checking for a local MayFS Update.bin against Update.may.
 * A matching checksum is NOT a trusted release signature. */
enum {
    MAY_VERIFY_NOT_CHECKED=0, MAY_VERIFY_NOT_READY,
    MAY_VERIFY_FILE_MISSING, MAY_VERIFY_SIZE_MISMATCH,
    MAY_VERIFY_HASH_MISMATCH, MAY_VERIFY_HASH_MATCH
};
int may_update_verify_local(void);
const char *may_update_verify_status(void);

/* Authenticates exact bounded Update.may bytes by a build-pinned Ed25519
 * release key, AND checks Update.bin against its declared SHA-256.
 * This DOES NOT install or download. Disabled without owner-pinned root. */
int may_update_authenticate_local(void);
const char *may_update_auth_status(void);
/* Signed 4KiB+ ATA staging integrity + pinned Ed25519 QEMU diagnostic ONLY.
 * Never installs images or changes boot state. Disabled in ordinary builds. */
int may_update_authenticate_staged(void);
const char *may_update_auth_staged_status(void);

/* Only for explicitly flagged QEMU diagnostics with temporary signing key.
 * Real update installation is unavailable. */
#ifdef MAYOS_SIGNED_STREAM_DIAGNOSTIC
int may_update_signed_stream_diagnostic(void);
const char *may_update_signed_stream_status(void);
#endif

#endif

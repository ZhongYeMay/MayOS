/* MayOS Linux64 opt-in persistent desktop store.
 * Explicitly mounted ext4 volume only; no block device scanning/writes.
 * Two checksum-validated atomic snapshots, highest valid generation wins.
 * Never overwrite both-invalid snapshots, or follow symlinks.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../kernel/core.h"
#undef memcpy
#undef memset
#undef strcmp
#undef strlen
#include "profile_persist.h"

#define PROFILE_SCHEMA 2u
#define PROFILE_PREFS 32u
#define PROFILE_MAGIC "MAYDTA01"
#define PROFILE_MARKER ".mayos-profile-enabled"
#define PROFILE_MARKER_VALUE "MayOS-Profile-v1\n"
#define PROFILE_SLOT_A "desktop-state-a.bin"
#define PROFILE_SLOT_B "desktop-state-b.bin"
#define PROFILE_TEMP "desktop-state.tmp"

typedef struct {
    char magic[8];
    uint32_t schema;
    uint32_t data_size;
    uint64_t generation;
    uint32_t crc32;
    uint32_t prefs_valid;
    uint32_t file_count;
    uint32_t reserved;
} ProfileHeader;
typedef struct {
    ProfileHeader header;
    File records[FS_MAX];
    uint8_t prefs[PROFILE_PREFS];
} ProfileImage;

_Static_assert(sizeof(ProfileHeader) == 40, "snapshot header ABI");
_Static_assert(sizeof(LegacyFile) == 4160, "legacy desktop file ABI");
_Static_assert(sizeof(ProfileImage) == 40 + FS_MAX * sizeof(File) + PROFILE_PREFS,
               "snapshot stable size");

typedef struct {
    ProfileHeader header;
    LegacyFile records[LEGACY_FS_MAX];
    uint8_t prefs[PROFILE_PREFS];
} LegacyProfileImage;
static LegacyProfileImage old_snapshot;

static ProfileImage candidates[2];
static ProfileImage snapshot;
static int directory_fd = -2;  /* -2 = unopened; -1 = disabled */
static int broken;
static int chosen_slot = -1;
static uint64_t generation;

/* Fault hooks are compiled into host-side tests ONLY. The production MayOS
 * Linux64 binary cannot enable crash/ENOSPC injection via environment input. */
#ifdef MAYOS_PROFILE_TESTING
static void profile_test_crash(const char *stage) {
    const char *requested = getenv("MAYOS_PROFILE_TEST_CRASH");
    if (requested && strcmp(requested, stage) == 0) _exit(97);
}
#define PROFILE_TEST_CRASH(stage) profile_test_crash(stage)
#else
#define PROFILE_TEST_CRASH(stage) ((void)0)
#endif

static uint32_t crc_update(uint32_t crc, const void *bytes, size_t size) {
    const uint8_t *data = bytes;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
static uint32_t profile_crc(ProfileHeader *header, size_t bytes) {
    uint32_t saved = header->crc32;
    header->crc32 = 0;
    uint32_t crc = ~crc_update(~0u, header, bytes);
    header->crc32 = saved;
    return crc;
}
static int exactly_read(int fd, void *buffer, size_t size) {
    uint8_t *at = buffer;
    while (size) {
        ssize_t count = read(fd, at, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        at += count;
        size -= (size_t)count;
    }
    return 1;
}
static int exactly_write(int fd, const void *buffer, size_t size) {
#ifdef MAYOS_PROFILE_TESTING
    const char *fault = getenv("MAYOS_PROFILE_TEST_ERROR");
    if (fault && strcmp(fault, "partial_enospc") == 0) {
        size_t count = size < 4096 ? size : 4096;
        if (write(fd, buffer, count) != (ssize_t)count) return 0;
        errno = ENOSPC;  /* Simulate a partially written full filesystem. */
        return 0;
    }
#endif
    const uint8_t *at = buffer;
    while (size) {
        ssize_t count = write(fd, at, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        at += count;
        size -= (size_t)count;
    }
    return 1;
}
static int validate_records(const File records[FS_MAX]) {
    /* Root and all protected system directories must remain intact. */
    for (int i = 0; i < 4; i++)
        if (records[i].used != 1 || records[i].dir != 1 ||
            records[i].parent != 0 || records[i].size != 0)
            return 0;
    for (int i = 0; i < FS_MAX; i++) {
        const File *f = &records[i];
        if (!f->used) continue;
        if (f->used != 1 || f->dir > 1 || f->size >= FILE_CAP ||
            f->name[sizeof(f->name) - 1] != '\0')
            return 0;
        if (!f->dir && f->data[f->size] != 0) return 0;
        if (i && (f->parent >= FS_MAX ||
                   !records[f->parent].used ||
                   !records[f->parent].dir))
            return 0;
        int current = i, depth = 0;
        while (current) {
            unsigned parent = records[current].parent;
            if (parent >= FS_MAX || !records[parent].used || !records[parent].dir ||
                ++depth >= FS_MAX) return 0;
            current = (int)parent;
        }
    }
    return 1;
}
static int decode_legacy(ProfileImage *image) {
    ProfileHeader *header = &old_snapshot.header;
    if (memcmp(header->magic, PROFILE_MAGIC, 8) || header->schema != 1 ||
        header->data_size != sizeof(old_snapshot.records) + PROFILE_PREFS ||
        header->file_count != LEGACY_FS_MAX || header->prefs_valid > 1 ||
        header->reserved || header->crc32 != profile_crc(header, sizeof(old_snapshot)))
        return 0;
    memset(image, 0, sizeof(*image));
    image->header = *header;
    memcpy(image->prefs, old_snapshot.prefs, PROFILE_PREFS);
    for (int i = 0; i < LEGACY_FS_MAX; i++) {
        const LegacyFile *old = &old_snapshot.records[i];
        if (!old->used) continue;
        if (old->size >= LEGACY_FILE_CAP || old->data[old->size]) return 0;
        File *file = &image->records[i];
        file->used = old->used;
        file->dir = old->dir;
        file->parent = old->parent;
        file->size = old->size;
        memcpy(file->name, old->name, sizeof(file->name));
        memcpy(file->data, old->data, old->size);
    }
    return validate_records(image->records);
}

static int load_slot(const char *name, ProfileImage *image, int *exists) {
    *exists = 0;
    int fd = openat(directory_fd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT) *exists = 1;
        return 0;
    }
    *exists = 1;
    struct stat stat_info;
    if (fstat(fd, &stat_info) || !S_ISREG(stat_info.st_mode) ||
        stat_info.st_uid != geteuid()) {
        close(fd);
        return 0;
    }
    if (stat_info.st_size == (off_t)sizeof(old_snapshot)) {
        int ok = exactly_read(fd, &old_snapshot, sizeof(old_snapshot));
        close(fd);
        return ok && decode_legacy(image);
    }
    int ok = stat_info.st_size == (off_t)sizeof(*image) && exactly_read(fd, image, sizeof(*image));
    close(fd);
    if (!ok) return 0;
    if (memcmp(image->header.magic, PROFILE_MAGIC, 8) != 0 ||
        image->header.schema != PROFILE_SCHEMA ||
        image->header.data_size != sizeof(image->records) + PROFILE_PREFS ||
        image->header.file_count != FS_MAX ||
        image->header.prefs_valid > 1 ||
        image->header.reserved != 0 ||
        image->header.crc32 != profile_crc(&image->header, sizeof(*image)) ||
        !validate_records(image->records))
        return 0;
    return 1;
}
static int open_profile_dir(void) {
    if (directory_fd != -2) return directory_fd >= 0;
    directory_fd = -1;
    const char *path = getenv("MAYOS_PROFILE_DIR");
    if (!path || path[0] != '/' || strlen(path) > 250) return 0;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat stat_info;
    if (fstat(fd, &stat_info) || !S_ISDIR(stat_info.st_mode) ||
        stat_info.st_uid != geteuid() || (stat_info.st_mode & 0022)) {
        close(fd); return 0;
    }
    /* The marker must exist on a deliberately prepared disk, not initramfs.
     * The boot script only exports MAYOS_PROFILE_DIR after checking ext4.
     */
    int marker = openat(fd, PROFILE_MARKER, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (marker < 0) { close(fd); return 0; }
    struct stat mark_stat;
    char text[sizeof(PROFILE_MARKER_VALUE)] = {0};
    int valid = fstat(marker, &mark_stat) == 0 &&
                S_ISREG(mark_stat.st_mode) && mark_stat.st_uid == geteuid() &&
                mark_stat.st_size == (off_t)(sizeof(PROFILE_MARKER_VALUE) - 1) &&
                exactly_read(marker, text, sizeof(PROFILE_MARKER_VALUE) - 1) &&
                memcmp(text, PROFILE_MARKER_VALUE, sizeof(PROFILE_MARKER_VALUE)-1) == 0;
    close(marker);
    if (!valid || flock(fd, LOCK_EX | LOCK_NB)) { close(fd); return 0; }
    directory_fd = fd;
    return 1;
}
int mayos_linux_profile_enabled(void) {
    return open_profile_dir() && !broken;
}
int mayos_linux_profile_load(File records[FS_MAX], u8 prefs[32], int *prefs_valid) {
    if (!open_profile_dir()) return 0;
    int exists_a, exists_b;
    int good_a = load_slot(PROFILE_SLOT_A, &candidates[0], &exists_a);
    int good_b = load_slot(PROFILE_SLOT_B, &candidates[1], &exists_b);
    if (!good_a && !good_b) {
        if (exists_a || exists_b) broken = 1;  /* Never overwrite corrupt data. */
        return 0;
    }
    int which = good_b && (!good_a ||
         candidates[1].header.generation > candidates[0].header.generation) ? 1 : 0;
    chosen_slot = which;
    generation = candidates[which].header.generation;
    memcpy(records, candidates[which].records, sizeof(candidates[which].records));
    memcpy(prefs, candidates[which].prefs, PROFILE_PREFS);
    *prefs_valid = (int)candidates[which].header.prefs_valid;
    return 1;
}
int mayos_linux_profile_save(const File records[FS_MAX], const u8 prefs[32],
                             int prefs_valid) {
    if (!mayos_linux_profile_enabled() || !validate_records(records)) return 0;
    if (generation == UINT64_MAX) return 0;
    memset(&snapshot, 0, sizeof(snapshot));
    memcpy(snapshot.header.magic, PROFILE_MAGIC, 8);
    snapshot.header.schema = PROFILE_SCHEMA;
    snapshot.header.data_size = sizeof(snapshot.records) + PROFILE_PREFS;
    snapshot.header.generation = generation + 1;
    snapshot.header.file_count = FS_MAX;
    snapshot.header.prefs_valid = prefs_valid ? 1u : 0u;
    memcpy(snapshot.records, records, sizeof(snapshot.records));
    memcpy(snapshot.prefs, prefs, PROFILE_PREFS);
    snapshot.header.crc32 = profile_crc(&snapshot.header, sizeof(snapshot));
    /* Previous interrupted temp file is never trusted; unlinkat cannot follow
     * a symlink and does not touch either committed snapshot. */
    if (unlinkat(directory_fd, PROFILE_TEMP, 0) && errno != ENOENT) return 0;
    int fd = openat(directory_fd, PROFILE_TEMP,
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    PROFILE_TEST_CRASH("temp_opened");
    int ok = exactly_write(fd, &snapshot, sizeof(snapshot));
    PROFILE_TEST_CRASH("temp_written");
    if (ok) ok = fsync(fd) == 0;
    PROFILE_TEST_CRASH("file_synced");
    if (close(fd)) ok = 0;
    if (!ok) { unlinkat(directory_fd, PROFILE_TEMP, 0); return 0; }
    int next = chosen_slot == 0 ? 1 : 0;
    const char *target = next ? PROFILE_SLOT_B : PROFILE_SLOT_A;
    if (renameat(directory_fd, PROFILE_TEMP, directory_fd, target)) {
        unlinkat(directory_fd, PROFILE_TEMP, 0); return 0;
    }
    PROFILE_TEST_CRASH("renamed");
    if (fsync(directory_fd)) return 0;
    PROFILE_TEST_CRASH("directory_synced");
    chosen_slot = next;
    generation++;
    return 1;
}

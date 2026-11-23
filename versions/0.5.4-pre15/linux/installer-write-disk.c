/* Offline installer writer. Build statically for the private initramfs. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include "installer-gpt.h"

enum {
    TEMPLATE_SIZE = 80 * 1024 * 1024,
    CHUNK = 1024 * 1024,
};

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void transfer(int fd, void *buffer, size_t size, uint64_t offset, int write) {
    unsigned char *cursor = buffer;
    while (size) {
        ssize_t result = write ? pwrite(fd, cursor, size, (off_t)offset)
                               : pread(fd, cursor, size, (off_t)offset);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) fail(write ? "Disk write failed." : "Disk read failed.");
        cursor += result;
        offset += (uint64_t)result;
        size -= (size_t)result;
    }
}

static int report_progress;

static void progress(const char *phase, uint64_t done, uint64_t total, int *last) {
    if (!report_progress) return;
    int percent = (int)((__uint128_t)done * 100 / total);
    if (percent != 100 && *last >= 0 && percent < *last + 5) return;
    if (percent == *last) return;
    *last = percent;
    printf("@PROGRESS %s %d\n", phase, percent);
}

static void compare(int fd, const unsigned char *expected, unsigned char *buffer,
                    size_t size, uint64_t offset, uint64_t total, int *last) {
    uint64_t compared = 0;
    while (size) {
        size_t count = size > CHUNK ? CHUNK : size;
        transfer(fd, buffer, count, offset, 0);
        if (memcmp(expected, buffer, count))
            fail("Disk readback did not match the installation image.");
        expected += count;
        offset += count;
        size -= count;
        compared += count;
        /* The final 100% is emitted separately after both GPT regions match. */
        if (last) progress("verify", compared, total, last);
    }
}

static void verify_layout(int disk, uint64_t sectors) {
    static const unsigned char esp[16] = {
        0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
        0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
    };
    static const unsigned char mayfs[16] = {
        0x4f, 0x59, 0x41, 0x4d, 0x2d, 0x53, 0x53, 0x46,
        0x8c, 0x4d, 0x41, 0x59, 0x46, 0x53, 0x30, 0x31
    };
    static const unsigned char empty[16];
    unsigned char first[GPT_HEAD_SIZE], last[GPT_TAIL_SIZE];
    transfer(disk, first, sizeof(first), 0, 0);
    transfer(disk, last, sizeof(last), (sectors * SECTOR) - sizeof(last), 0);
    unsigned char *primary = first + SECTOR, *entries = first + 2 * SECTOR;
    unsigned char *secondary = last + GPT_TABLE_SIZE;
    uint32_t primary_crc = get32(primary + 16), secondary_crc = get32(secondary + 16);
    put32(primary + 16, 0);
    put32(secondary + 16, 0);
    if (first[450] != 0xee ||
        memcmp(primary, "EFI PART", 8) || memcmp(secondary, "EFI PART", 8) ||
        get32(primary + 12) != 92 || get32(secondary + 12) != 92 ||
        crc32(primary, 92) != primary_crc || crc32(secondary, 92) != secondary_crc ||
        get64(primary + 24) != 1 || get64(primary + 32) != sectors - 1 ||
        get64(secondary + 24) != sectors - 1 || get64(secondary + 32) != 1 ||
        get64(primary + 72) != 2 || get64(secondary + 72) != sectors - 33 ||
        get32(primary + 80) != 128 || get32(primary + 84) != 128 ||
        crc32(entries, GPT_TABLE_SIZE) != get32(primary + 88) ||
        get32(primary + 88) != get32(secondary + 88) ||
        memcmp(primary + 56, secondary + 56, GPT_GUID_BYTES) ||
        memcmp(entries, last, GPT_TABLE_SIZE) ||
        memcmp(entries, esp, 16) || get64(entries + 32) != 2048 ||
        get64(entries + 40) != 133119 ||
        memcmp(entries + 128, mayfs, 16) || get64(entries + 160) != 133120 ||
        get64(entries + 168) != 149503)
        fail("Existing disk is not a supported MayOS GPT installation.");
    for (unsigned offset = 256; offset < GPT_TABLE_SIZE; offset += 128)
        if (memcmp(entries + offset, empty, 16))
            fail("Additional GPT partitions are outside this upgrade layout.");
}

static void patch_efi_identity(unsigned char *image, const unsigned char *ids) {
    const unsigned max_bytes = 131072u * SECTOR;
    unsigned char *esp = image + 2048u * SECTOR;
    unsigned reserved = (unsigned)esp[14] | (unsigned)esp[15] << 8;
    unsigned backup_lba = (unsigned)esp[50] | (unsigned)esp[51] << 8;
    if (esp[11] != 0 || esp[12] != 2 || esp[66] != 0x29 || !backup_lba ||
        backup_lba >= reserved || reserved >= 131072u)
        fail("Installation image has an invalid FAT32 boot sector.");
    unsigned char *backup = esp + backup_lba * SECTOR;
    if (backup[66] != 0x29 || get32(esp + 67) != get32(backup + 67))
        fail("FAT32 primary and backup identities do not match.");

    static const char marker[] = "# MAYOS_RELOCATABLE_EFI_CONFIG_V1\n";
    const size_t marker_size = sizeof marker - 1;
    unsigned char *found = memmem(esp, max_bytes, marker, marker_size);
    if (!found || memmem(found + marker_size, (size_t)(esp + max_bytes - found) - marker_size,
                         marker, marker_size))
        fail("Installation image has no unique EFI identity record.");

    static const char volume_prefix[] = "set mayos_volume=";
    static const char esp_prefix[] = "\nset mayos_esp_expected=";
    const size_t prefix_size = sizeof volume_prefix - 1, second_size = sizeof esp_prefix - 1;
    const size_t record_size = prefix_size + 9 + second_size + 36 + 1;
    if ((size_t)(found - esp) < record_size) fail("EFI loader identity record is truncated.");
    unsigned char *record = found - record_size;
    unsigned char *volume_field = record + prefix_size;
    unsigned char *esp_field = volume_field + 9 + second_size;
    if (memcmp(record, volume_prefix, prefix_size) ||
        memcmp(volume_field + 9, esp_prefix, second_size) || esp_field[36] != '\n')
        fail("EFI loader identity record has an invalid format.");

    uint32_t serial = get32(ids);
    if (!serial) serial = get32(ids + 4) | 1u;
    put32(esp + 67, serial);
    put32(backup + 67, serial);
    char volume[10], uuid[37];
    static const unsigned char order[] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    const unsigned char *partition = ids + GPT_GUID_BYTES;
    snprintf(volume, sizeof volume, "%04X-%04X", serial >> 16, serial & 0xffffu);
    size_t offset = 0;
    for (unsigned i = 0; i < sizeof order; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) uuid[offset++] = '-';
        int count = snprintf(uuid + offset, sizeof uuid - offset, "%02x", partition[order[i]]);
        if (count != 2) fail("Could not format the EFI partition identity.");
        offset += 2;
    }
    memcpy(volume_field, volume, sizeof volume - 1);
    memcpy(esp_field, uuid, sizeof uuid - 1);
}

int main(int argc, char **argv) {
    int check = argc == 4 && !strcmp(argv[3], "check");
    if (argc != 3 && !check) fail("Expected a target device and its sysfs device ID.");
    unsigned want_major, want_minor;
    char extra;
    if (sscanf(argv[2], "%u:%u%c", &want_major, &want_minor, &extra) != 2)
        fail("Invalid target device ID.");
    int disk = open(argv[1], (check ? O_RDONLY : O_RDWR) | O_EXCL | O_NOFOLLOW | O_CLOEXEC);
    struct stat info;
    int sector = 0;
    uint64_t size = 0;
    if (disk < 0 || fstat(disk, &info) || !S_ISBLK(info.st_mode) ||
        major(info.st_rdev) != want_major || minor(info.st_rdev) != want_minor ||
        ioctl(disk, BLKSSZGET, &sector) || sector != SECTOR ||
        ioctl(disk, BLKGETSIZE64, &size) || size % SECTOR ||
        size < TEMPLATE_SIZE || size > (uint64_t)INT64_MAX)
        fail("Target is unavailable, busy or outside the supported disk layout.");
    if (check) {
        verify_layout(disk, size / SECTOR);
        return close(disk) ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    unsigned char *image = malloc(TEMPLATE_SIZE), *buffer = malloc(CHUNK);
    unsigned char *zero = calloc(1, CHUNK);
    if (!image || !buffer || !zero) fail("Not enough memory to stage the verified disk image.");
    int template = open("/installer/disk.img", O_RDONLY | O_CLOEXEC);
    if (template < 0 || fstat(template, &info) || info.st_size != TEMPLATE_SIZE)
        fail("Installation image is missing or truncated.");
    transfer(template, image, TEMPLATE_SIZE, 0, 0);
    close(template);
    unsigned char backup[GPT_TAIL_SIZE];
    memcpy(backup, image + TEMPLATE_SIZE - GPT_TAIL_SIZE, sizeof(backup));
    if (image[450] != 0xee || memcmp(image + SECTOR, "EFI PART", 8) ||
        memcmp(backup + GPT_TABLE_SIZE, "EFI PART", 8))
        fail("Installation image has an unexpected GPT layout.");
    resize_gpt(image, backup, size / SECTOR);
    memset(image + TEMPLATE_SIZE - GPT_TAIL_SIZE, 0, GPT_TAIL_SIZE);

    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *progress_mode = getenv("MAYOS_INSTALLER_PROGRESS");
    report_progress = progress_mode && !strcmp(progress_mode, "1");
    puts("Checking the entire target for existing data...");
    int last = -1;
    progress("scan", 0, size, &last);
    for (uint64_t offset = 0; offset < size; offset += CHUNK) {
        if (offset && offset % (1024ull * CHUNK) == 0)
            printf("Checked %llu MiB\n", (unsigned long long)(offset / CHUNK));
        size_t count = size - offset > CHUNK ? CHUNK : (size_t)(size - offset);
        transfer(disk, buffer, count, offset, 0);
        if (memcmp(buffer, zero, count))
            fail("Target contains data. No sectors were written.");
        progress("scan", offset + count, size, &last);
    }

    unsigned char guid[GPT_INSTALL_GUIDS * GPT_GUID_BYTES];
    size_t random_bytes = 0;
    while (random_bytes < sizeof guid) {
        ssize_t received = getrandom(guid + random_bytes, sizeof guid - random_bytes, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) fail("Could not generate an installation disk identity. No sectors were written.");
        random_bytes += (size_t)received;
    }
    install_gpt_identity(image, backup, guid);
    patch_efi_identity(image, guid);

    puts("Writing and verifying the installed MayOS disk...");
    size_t body_end = size == TEMPLATE_SIZE ? TEMPLATE_SIZE - GPT_TAIL_SIZE : TEMPLATE_SIZE;
    uint64_t total = body_end + sizeof(backup);
    last = -1;
    progress("write", 0, total, &last);
    for (size_t offset = GPT_HEAD_SIZE; offset < body_end;) {
        size_t count = body_end - offset > CHUNK ? CHUNK : body_end - offset;
        transfer(disk, image + offset, count, offset, 1);
        offset += count;
        progress("write", offset - GPT_HEAD_SIZE, total, &last);
    }
    transfer(disk, backup, sizeof(backup), size - GPT_TAIL_SIZE, 1);
    if (fsync(disk)) fail("Could not flush the disk and backup GPT.");
    transfer(disk, image, GPT_HEAD_SIZE, 0, 1);
    if (fsync(disk)) fail("Could not flush the installed partition table.");
    progress("write", total, total, &last);
    last = -1;
    progress("verify", 0, total, &last);
    compare(disk, image, buffer, body_end, 0, total, &last);
    compare(disk, backup, buffer, sizeof(backup), size - GPT_TAIL_SIZE, total, 0);
    progress("verify", total, total, &last);
    return close(disk) ? EXIT_FAILURE : EXIT_SUCCESS;
}

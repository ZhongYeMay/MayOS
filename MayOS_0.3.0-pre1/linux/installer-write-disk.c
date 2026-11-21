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
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

enum {
    SECTOR = 512,
    TEMPLATE_SIZE = 80 * 1024 * 1024,
    CHUNK = 1024 * 1024,
    GPT_TABLE_SIZE = 128 * 128,
    GPT_TAIL_SIZE = GPT_TABLE_SIZE + SECTOR,
    GPT_HEAD_SIZE = 2 * SECTOR + GPT_TABLE_SIZE,
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

static uint32_t crc32(const unsigned char *bytes, size_t size) {
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *bytes++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put32(unsigned char *bytes, uint32_t value) { memcpy(bytes, &value, 4); }
static void put64(unsigned char *bytes, uint64_t value) { memcpy(bytes, &value, 8); }
static uint32_t get32(const unsigned char *bytes) {
    uint32_t value;
    memcpy(&value, bytes, 4);
    return value;
}
static uint64_t get64(const unsigned char *bytes) {
    uint64_t value;
    memcpy(&value, bytes, 8);
    return value;
}

static void header_crc(unsigned char *header) {
    put32(header + 16, 0);
    put32(header + 16, crc32(header, 92));
}

static void compare(int fd, const unsigned char *expected, unsigned char *buffer,
                    size_t size, uint64_t offset) {
    while (size) {
        size_t count = size > CHUNK ? CHUNK : size;
        transfer(fd, buffer, count, offset, 0);
        if (memcmp(expected, buffer, count))
            fail("Disk readback did not match the installation image.");
        expected += count;
        offset += count;
        size -= count;
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
        size < TEMPLATE_SIZE || size / SECTOR > 0x0fffffffu)
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
    unsigned char *primary = image + SECTOR;
    memcpy(backup, image + TEMPLATE_SIZE - GPT_TAIL_SIZE, sizeof(backup));
    unsigned char *secondary = backup + GPT_TABLE_SIZE;
    if (image[450] != 0xee || memcmp(primary, "EFI PART", 8) ||
        memcmp(secondary, "EFI PART", 8))
        fail("Installation image has an unexpected GPT layout.");
    uint64_t sectors = size / SECTOR;
    put32(image + 458, (uint32_t)(sectors - 1));
    put64(primary + 32, sectors - 1);
    put64(primary + 48, sectors - 34);
    put64(secondary + 24, sectors - 1);
    put64(secondary + 48, sectors - 34);
    put64(secondary + 72, sectors - 33);
    header_crc(primary);
    header_crc(secondary);
    memset(image + TEMPLATE_SIZE - GPT_TAIL_SIZE, 0, GPT_TAIL_SIZE);

    setvbuf(stdout, NULL, _IOLBF, 0);
    puts("Checking the entire target for existing data...");
    for (uint64_t offset = 0; offset < size; offset += CHUNK) {
        if (offset && offset % (1024ull * CHUNK) == 0)
            printf("Checked %llu MiB\n", (unsigned long long)(offset / CHUNK));
        size_t count = size - offset > CHUNK ? CHUNK : (size_t)(size - offset);
        transfer(disk, buffer, count, offset, 0);
        if (memcmp(buffer, zero, count))
            fail("Target contains data. No sectors were written.");
    }

    puts("Writing and verifying the installed MayOS disk...");
    size_t body_end = size == TEMPLATE_SIZE ? TEMPLATE_SIZE - GPT_TAIL_SIZE : TEMPLATE_SIZE;
    transfer(disk, image + GPT_HEAD_SIZE, body_end - GPT_HEAD_SIZE, GPT_HEAD_SIZE, 1);
    transfer(disk, backup, sizeof(backup), size - GPT_TAIL_SIZE, 1);
    if (fsync(disk)) fail("Could not flush the disk and backup GPT.");
    transfer(disk, image, GPT_HEAD_SIZE, 0, 1);
    if (fsync(disk)) fail("Could not flush the installed partition table.");
    compare(disk, image, buffer, body_end, 0);
    compare(disk, backup, buffer, sizeof(backup), size - GPT_TAIL_SIZE);
    return close(disk) ? EXIT_FAILURE : EXIT_SUCCESS;
}

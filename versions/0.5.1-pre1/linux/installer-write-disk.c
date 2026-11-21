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

int main(int argc, char **argv) {
    if (argc != 3) fail("Expected a target device and its sysfs device ID.");
    unsigned want_major, want_minor;
    char extra;
    if (sscanf(argv[2], "%u:%u%c", &want_major, &want_minor, &extra) != 2)
        fail("Invalid target device ID.");
    int disk = open(argv[1], O_RDWR | O_EXCL | O_NOFOLLOW | O_CLOEXEC);
    struct stat info;
    int sector = 0;
    uint64_t size = 0;
    if (disk < 0 || fstat(disk, &info) || !S_ISBLK(info.st_mode) ||
        major(info.st_rdev) != want_major || minor(info.st_rdev) != want_minor ||
        ioctl(disk, BLKSSZGET, &sector) || sector != SECTOR ||
        ioctl(disk, BLKGETSIZE64, &size) || size % SECTOR ||
        size < TEMPLATE_SIZE || size / SECTOR > 0x0fffffffu)
        fail("Target is unavailable, busy or outside the supported disk layout.");

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

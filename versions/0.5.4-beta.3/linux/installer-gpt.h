#ifndef MAYOS_INSTALLER_GPT_H
#define MAYOS_INSTALLER_GPT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    SECTOR_512 = 512,
    SECTOR_4K = 4096,
    GPT_TABLE_SIZE = 128 * 128,
    GPT_HEAD_MAX = GPT_TABLE_SIZE + 2 * SECTOR_4K,
    GPT_TAIL_MAX = GPT_TABLE_SIZE + SECTOR_4K,
};

static inline size_t gpt_head_size(unsigned sector) { return GPT_TABLE_SIZE + 2 * sector; }
static inline size_t gpt_tail_size(unsigned sector) { return GPT_TABLE_SIZE + sector; }

static inline uint32_t crc32(const unsigned char *bytes, size_t size) {
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *bytes++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static inline void put32(unsigned char *bytes, uint32_t value) { memcpy(bytes, &value, 4); }
static inline void put64(unsigned char *bytes, uint64_t value) { memcpy(bytes, &value, 8); }
static inline uint32_t get32(const unsigned char *bytes) {
    uint32_t value;
    memcpy(&value, bytes, 4);
    return value;
}
static inline uint64_t get64(const unsigned char *bytes) {
    uint64_t value;
    memcpy(&value, bytes, 8);
    return value;
}

static inline void header_crc(unsigned char *header) {
    put32(header + 16, 0);
    put32(header + 16, crc32(header, 92));
}

/* Relocate the mirrored GPT to the end of a larger device with the same logical sector size. The protective MBR has a 32-bit size field and must saturate. */
static inline void resize_gpt(unsigned char *front, unsigned char *backup, uint64_t sectors, unsigned sector) {
    unsigned char *primary = front + sector;
    unsigned char *secondary = backup + GPT_TABLE_SIZE;
    uint64_t end = sectors - 1;
    uint64_t table_blocks = GPT_TABLE_SIZE / sector;
    put32(front + 458, end > UINT32_MAX ? UINT32_MAX : (uint32_t)end);
    put64(primary + 32, end);
    put64(primary + 48, end - table_blocks - 1);
    put64(secondary + 24, end);
    put64(secondary + 48, end - table_blocks - 1);
    put64(secondary + 72, end - table_blocks);
    header_crc(primary);
    header_crc(secondary);
}

/* GPT stores the first three UUID fields little endian. Set UUIDv4
 * version/variant bits in that byte order and update both entry arrays. */
enum { GPT_INSTALL_GUIDS = 3, GPT_GUID_BYTES = 16 };
static inline void install_gpt_identity(unsigned char *front, unsigned char *backup,
                                        unsigned char guid[GPT_INSTALL_GUIDS * GPT_GUID_BYTES],
                                        unsigned sector) {
    unsigned char *primary = front + sector;
    unsigned char *secondary = backup + GPT_TABLE_SIZE;
    unsigned char *entries = front + 2 * sector;
    for (int i = 0; i < GPT_INSTALL_GUIDS; i++) {
        unsigned char *id = guid + i * GPT_GUID_BYTES;
        id[7] = (id[7] & 0x0f) | 0x40;
        id[8] = (id[8] & 0x3f) | 0x80;
    }
    memcpy(primary + 56, guid, GPT_GUID_BYTES);
    memcpy(secondary + 56, guid, GPT_GUID_BYTES);
    memcpy(entries + 16, guid + GPT_GUID_BYTES, GPT_GUID_BYTES);
    memcpy(entries + 128 + 16, guid + 2 * GPT_GUID_BYTES, GPT_GUID_BYTES);
    memcpy(backup, entries, GPT_TABLE_SIZE);
    uint32_t checksum = crc32(entries, GPT_TABLE_SIZE);
    put32(primary + 88, checksum);
    put32(secondary + 88, checksum);
    header_crc(primary);
    header_crc(secondary);
}

#endif

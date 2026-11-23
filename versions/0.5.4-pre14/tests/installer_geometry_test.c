#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../linux/installer-gpt.h"

static void verify_crc(unsigned char *header) {
    uint32_t saved = get32(header + 16);
    put32(header + 16, 0);
    assert(crc32(header, 92) == saved);
}

static void verify_size(uint64_t sectors, uint32_t mbr_size) {
    unsigned char front[GPT_HEAD_SIZE] = {0}, backup[GPT_TAIL_SIZE] = {0};
    unsigned char *primary = front + SECTOR, *secondary = backup + GPT_TABLE_SIZE;
    put64(primary + 24, 1);
    put64(secondary + 32, 1);
    front[450] = 0xee;
    primary[56] = secondary[56] = 0x45; /* Disk GUID is retained. */
    resize_gpt(front, backup, sectors);
    assert(front[450] == 0xee && get32(front + 458) == mbr_size);
    assert(get64(primary + 32) == sectors - 1 && get64(primary + 48) == sectors - 34);
    assert(get64(secondary + 24) == sectors - 1 && get64(secondary + 48) == sectors - 34);
    assert(get64(secondary + 72) == sectors - 33);
    assert(get64(primary + 24) == 1 && get64(secondary + 32) == 1);
    assert(primary[56] == 0x45 && secondary[56] == 0x45);
    verify_crc(primary);
    verify_crc(secondary);
}

int main(void) {
    assert(crc32((const unsigned char *)"123456789", 9) == 0xcbf43926u);
    verify_size(163840u, 163839u);
    verify_size(1ull << 30, (1u << 30) - 1); /* 512 GiB */
    verify_size(3ull << 31, UINT32_MAX);     /* 3 TiB */
    puts("PASS: 80 MiB / 512 GiB / 3 TiB GPT geometry and protective MBR");
    return 0;
}

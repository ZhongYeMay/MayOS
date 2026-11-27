#include "mmio.h"

#define TABLE_ADDRESS 0x000ffffffffff000ULL
#define PHYSICAL_END (1ULL << 52)
#define MMIO_WINDOW 0xffff800000000000ULL

static u64 next_virtual = MMIO_WINDOW;

static u64 *table_child(u64 *table, u32 slot) {
    if (table[slot] & 1u) return (u64 *)(uptr)(table[slot] & TABLE_ADDRESS);
    u64 *page = page_alloc();
    if (!page) return 0;
    table[slot] = (u64)(uptr)page | 3u;
    return page;
}

volatile void *mmio_map(u64 physical, u32 bytes) {
    if (!bytes || physical >= PHYSICAL_END || bytes > PHYSICAL_END - physical) return 0;
    u64 first = physical & ~4095ULL;
    u64 span = ((physical - first) + bytes + 4095) & ~4095ULL;
    u64 start = next_virtual;
    if (span > (1ULL << 30) - (start - MMIO_WINDOW)) return 0;
    next_virtual += span;
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    u64 *pml4 = (u64 *)(uptr)(cr3 & TABLE_ADDRESS);
    u64 *pdpt = table_child(pml4, 256);
    if (!pdpt) return 0;
    u64 *directory = table_child(pdpt, 0);
    if (!directory) return 0;
    for (u64 offset = 0; offset < span; offset += 4096) {
        u64 address = start + offset;
        u64 *table = table_child(directory, (u32)((address >> 21) & 511));
        if (!table) return 0;
        /* Default x86 PAT slot 3 is uncacheable (PCD=1, PWT=1). */
        table[(address >> 12) & 511] = (first + offset) | 0x1bu;
        __asm__ volatile("invlpg (%0)" :: "r"((uptr)address) : "memory");
    }
    return (volatile void *)(uptr)(start + (physical - first));
}

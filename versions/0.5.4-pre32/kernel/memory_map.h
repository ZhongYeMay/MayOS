#ifndef MAYOS_MEMORY_MAP_H
#define MAYOS_MEMORY_MAP_H
#include "core.h"

#define MAY_PAGE_BYTES 4096u
#define MAY_DMA32_PAGES (1u << 20)
#define MAY_MEMORY_RANGES 256u

typedef struct {
    u64 first, end;
    int usable;
} MayMemoryRange;

/* Work in whole pages. A partial reserved page is never allocatable. */
static inline MayMemoryRange may_memory_range(u64 base, u64 length, int usable) {
    const u64 limit = 1ULL << 52;
    if (!length || base >= limit) return (MayMemoryRange){0};
    u64 end = length > limit - base ? limit : base + length;
    u64 first_page = (base + (usable ? MAY_PAGE_BYTES - 1 : 0)) / MAY_PAGE_BYTES;
    u64 end_page = (end + (usable ? 0 : MAY_PAGE_BYTES - 1)) / MAY_PAGE_BYTES;
    return (MayMemoryRange){first_page, end_page, usable};
}

/* Return each disjoint usable run once, with reserved entries overriding RAM. */
static inline u64 may_memory_walk(const MayMemoryRange *map, u32 count,
                                  void (*usable_run)(u64 first, u64 end)) {
    u64 cursor = 0, total = 0;
    for (;;) {
        u64 boundary = ~0ULL;
        int usable = 0, reserved = 0;
        for (u32 i = 0; i < count; i++) {
            const MayMemoryRange *range = &map[i];
            if (range->first >= range->end || range->end <= cursor) continue;
            if (range->first > cursor) {
                if (range->first < boundary) boundary = range->first;
            } else {
                if (range->end < boundary) boundary = range->end;
                if (range->usable) usable = 1;
                else reserved = 1;
            }
        }
        if (boundary == ~0ULL) break;
        if (usable && !reserved) {
            total += boundary - cursor;
            if (usable_run) usable_run(cursor, boundary);
        }
        cursor = boundary;
    }
    return total;
}
#endif

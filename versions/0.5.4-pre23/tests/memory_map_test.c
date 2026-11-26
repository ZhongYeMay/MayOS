#include "memory_map.h"

static u64 sampled;
static void count_run(u64 first, u64 end) { sampled += end - first; }

int main(void) {
    const u64 gib = 1ULL << 30;
    const MayMemoryRange entries[] = {
        may_memory_range(8 * gib, gib, 1),
        may_memory_range(4 * gib, 2 * gib, 1),
        may_memory_range(3 * gib, 2 * gib, 1),
        may_memory_range(4 * gib + 1, MAY_PAGE_BYTES, 0),
        may_memory_range(9 * gib, 0, 0),
    };
    u64 total = may_memory_walk(entries, sizeof(entries) / sizeof(entries[0]), count_run);
    if (total != (4 * gib / MAY_PAGE_BYTES) - 2 || sampled != total) return 1;
    const MayMemoryRange partial = may_memory_range(1, MAY_PAGE_BYTES, 1);
    if (partial.first != partial.end) return 2;
    const MayMemoryRange overflow = may_memory_range(~0ULL - 100, 1000, 1);
    if (overflow.first != overflow.end) return 3;
    return 0;
}

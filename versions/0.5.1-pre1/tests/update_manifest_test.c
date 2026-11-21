#include "core.h"
#include "update.h"

/* Host-only deterministic tests. No QEMU, network, or physical disk needed. */
File files[FS_MAX];

void *memset(void *dst, int value, u32 size) {
    u8 *out = dst;
    for (u32 i = 0; i < size; i++) out[i] = (u8)value;
    return dst;
}
u32 strlen(const char *s) {
    u32 n = 0;
    while (s[n]) n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
int fs_find(int parent, const char *name) {
    return parent == 1 && files[4].used &&
                   !strcmp(name, "Update.may") ? 4 : -1;
}

static const char good[] =
    "name=MayOS\n"
    "version=0.5.1-pre2\n"
    "architecture=x86_64\n"
    "kernel=MayKernel\n"
    "release_date=2026-11-18\n"
    "size_bytes=1048576\n"
    "sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
    "eta_minutes=15\n"
    "changelog=Fix startup and storage\n";

static void load(const char *manifest) {
    memset(files, 0, sizeof(files));
    files[4].used = 1;
    u32 n = strlen(manifest);
    for (u32 i = 0; i < n; i++) files[4].data[i] = (u8)manifest[i];
    files[4].size = n;
}
static int change(const char *from, const char *to) {
    u32 length = strlen(from);
    if (length != strlen(to)) return 0;
    for (u32 i = 0; i + length <= files[4].size; i++) {
        u32 j = 0;
        while (j < length && files[4].data[i + j] == (u8)from[j]) j++;
        if (j == length) {
            for (j = 0; j < length; j++)
                files[4].data[i + j] = (u8)to[j];
            return 1;
        }
    }
    return 0;
}
static int expect(MayUpdateState expected) {
    may_update_check();
    return may_update_info()->state == expected;
}
int main(void) {
    memset(files, 0, sizeof(files));
    if (!expect(MAY_UPDATE_MISSING)) return 1;
    load(good);
    if (!expect(MAY_UPDATE_CANDIDATE)) return 2;
    if (strcmp(may_update_info()->version, "0.5.1-pre2")) return 3;
    if (may_update_info()->size_bytes != 1048576u) return 4;

    load(good);
    if (!change("0.5.1-pre2", "0.5.0-pre1") ||
        !expect(MAY_UPDATE_CURRENT)) return 5;
    load(good);
    if (!change("0.5.1-pre2", "0.5.1-pre1") ||
        !expect(MAY_UPDATE_CURRENT)) return 6;

    load(good);
    if (!change("x86_64", "a86_64") ||
        !expect(MAY_UPDATE_INVALID)) return 7;
    load(good);
    if (!change("MayKernel", "MayKerneL") ||
        !expect(MAY_UPDATE_INVALID)) return 8;
    load(good);
    if (!change("sha256=0", "sha256=Z") ||
        !expect(MAY_UPDATE_INVALID)) return 9;
    load(good);
    if (!change("size_bytes=1048576", "size_bytes=x048576") ||
        !expect(MAY_UPDATE_INVALID)) return 10;
    load(good);
    if (!change("2026-11-18", "2026-99-18") ||
        !expect(MAY_UPDATE_INVALID)) return 11;
    load(good);
    const char duplicate[] = "name=Injected\n";
    u32 len = strlen(duplicate);
    for (u32 i = 0; i < len; i++)
        files[4].data[files[4].size + i] = (u8)duplicate[i];
    files[4].size += len;
    if (!expect(MAY_UPDATE_INVALID)) return 12;

    load(good);
    if (!change("2026-11-18", "2026-02-30") ||
        !expect(MAY_UPDATE_INVALID)) return 15;
    load(good);
    if (!change("2026-11-18", "2028-02-29") ||
        !expect(MAY_UPDATE_CANDIDATE)) return 16;
    load(good);
    if (!change("2026-11-18", "2100-02-29") ||
        !expect(MAY_UPDATE_INVALID)) return 17;
    load(good);
    if (!change("name=MayOS", "name=FakeO") ||
        !expect(MAY_UPDATE_INVALID)) return 18;

    load(good);
    files[4].size = 2049;
    if (!expect(MAY_UPDATE_INVALID)) return 13;
    load(good);
    files[4].data[0] = 0;
    if (!expect(MAY_UPDATE_INVALID)) return 14;
    return 0;
}

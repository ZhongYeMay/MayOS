/* Host-side executable tests for the actual Linux64 desktop snapshot code.
 * Uses disposable root-owned temporary directories; no block device involved. */
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../linux/profile_persist.h"
#undef memcpy
#undef memset
#undef strcmp
#undef strlen

static File desktop[FS_MAX];
static unsigned char prefs[32];
static int prefs_valid;
static void setup(const char *words) {
    memset(desktop, 0, sizeof(desktop));
    for (int i = 0; i < 4; i++) {
        desktop[i].used = desktop[i].dir = 1;
        desktop[i].parent = 0;
    }
    desktop[4].used = 1;
    desktop[4].parent = 1;
    strcpy(desktop[4].name, "notes.txt");
    strcpy((char *)desktop[4].data, words);
    desktop[4].size = (u32)strlen(words);
    prefs[0] = 0x4d;
    prefs[1] = (u8)strlen(words);
    prefs_valid = 1;
}
static void path_join(char *dest, size_t size, const char *dir, const char *name) {
    assert(snprintf(dest, size, "%s/%s", dir, name) > 0);
}
static void corrupt(const char *file) {
    int fd = open(file, O_WRONLY | O_NOFOLLOW);
    assert(fd >= 0);
    const char patch[] = "INVALID";
    assert(pwrite(fd, patch, sizeof(patch), 105) == (ssize_t)sizeof(patch));
    assert(fsync(fd) == 0);
    assert(close(fd) == 0);
}
static void put_file(const char *filename, const char *data) {
    int fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0);
    assert(write(fd, data, strlen(data)) == (ssize_t)strlen(data));
    assert(close(fd) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    setup("memory-only");
    if (!strcmp(argv[1], "--ram")) {
        unsetenv("MAYOS_PROFILE_DIR");
        assert(!mayos_linux_profile_enabled());
        assert(!mayos_linux_profile_save(desktop, prefs, prefs_valid));
        puts("PASS: default session has no persistent storage");
        return 0;
    }
    char dir[] = "/tmp/mayos-profile-XXXXXX";
    assert(mkdtemp(dir) != NULL);
    assert(chmod(dir, 0700) == 0);
    char marker[400], a[400], b[400], tmp[400];
    path_join(marker, sizeof(marker), dir, ".mayos-profile-enabled");
    path_join(a, sizeof(a), dir, "desktop-state-a.bin");
    path_join(b, sizeof(b), dir, "desktop-state-b.bin");
    path_join(tmp, sizeof(tmp), dir, "desktop-state.tmp");
    put_file(marker, !strcmp(argv[1], "--badmarker") ? "BAD\n" : "MayOS-Profile-v1\n");
    assert(setenv("MAYOS_PROFILE_DIR", dir, 1) == 0);
    if (!strcmp(argv[1], "--badmarker")) {
        assert(!mayos_linux_profile_enabled());
        assert(!mayos_linux_profile_save(desktop, prefs, prefs_valid));
        assert(access(a, F_OK) != 0 && access(b, F_OK) != 0);
        assert(unlink(marker) == 0);
        assert(rmdir(dir) == 0);
        puts("PASS: unmarked storage is never written");
        return 0;
    }
    assert(!strcmp(argv[1], "--snapshot"));
    assert(mayos_linux_profile_enabled());
    setup("first saved state");
    assert(mayos_linux_profile_save(desktop, prefs, prefs_valid));
    assert(access(a, R_OK) == 0 && access(b, F_OK) != 0);
    put_file(tmp, "incomplete previous write");
    setup("second saved state");
    assert(mayos_linux_profile_save(desktop, prefs, prefs_valid));
    assert(access(b, R_OK) == 0 && access(tmp, F_OK) != 0);
    memset(desktop, 0, sizeof(desktop));
    memset(prefs, 0, sizeof(prefs));
    prefs_valid = 0;
    assert(mayos_linux_profile_load(desktop, prefs, &prefs_valid));
    assert(!strcmp((char *)desktop[4].data, "second saved state"));
    assert(prefs_valid == 1 && prefs[1] == strlen("second saved state"));
    corrupt(b);
    memset(desktop, 0, sizeof(desktop));
    assert(mayos_linux_profile_load(desktop, prefs, &prefs_valid));
    assert(!strcmp((char *)desktop[4].data, "first saved state"));
    /* The good A copy is intact until the replacement B is fsync'ed. */
    setup("restored from backup");
    assert(mayos_linux_profile_save(desktop, prefs, prefs_valid));
    memset(desktop, 0, sizeof(desktop));
    assert(mayos_linux_profile_load(desktop, prefs, &prefs_valid));
    assert(!strcmp((char *)desktop[4].data, "restored from backup"));
    /* Both snapshots corrupt: refuse to destroy recovery evidence. */
    corrupt(a);
    corrupt(b);
    assert(!mayos_linux_profile_load(desktop, prefs, &prefs_valid));
    assert(!mayos_linux_profile_enabled());
    assert(!mayos_linux_profile_save(desktop, prefs, prefs_valid));
    assert(access(a, F_OK) == 0 && access(b, F_OK) == 0);
    unlink(a); unlink(b); unlink(marker);
    assert(rmdir(dir) == 0);
    puts("PASS: two atomic snapshots, prefs, power-loss temp, CRC fallback, double-corruption fail-closed");
    return 0;
}

/* Crash/ENOSPC fault-injection harness against real Linux64 profile backend.
 * A child exits abruptly at chosen commit boundary; parent emulates reboot by
 * reloading the same committed snapshots. This tests process crashes, NOT
 * hardware power cuts or real ext4 journal replay.
 * Compile with -DMAYOS_PROFILE_TESTING only in host CI.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../linux/profile_persist.h"
#undef memcpy
#undef memset
#undef strcmp
#undef strlen

static File files[FS_MAX];
static u8 prefs[32];

static void set_document(const char *message) {
    memset(files, 0, sizeof(files));
    memset(prefs, 0, sizeof(prefs));
    for (int i = 0; i < 4; i++) {
        files[i].used = 1;
        files[i].dir = 1;
        files[i].parent = 0;
    }
    files[4].used = 1;
    files[4].dir = 0;
    files[4].parent = 1;
    strcpy(files[4].name, "recovery.txt");
    size_t n = strlen(message);
    assert(n < FILE_CAP);
    memcpy(files[4].data, message, n + 1);
    files[4].size = (u32)n;
    prefs[0] = 0x5a;
}

static void root_marker(const char *root) {
    char filename[512];
    assert(snprintf(filename, sizeof(filename), "%s/.mayos-profile-enabled", root) < (int)sizeof(filename));
    int fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(fd >= 0);
    const char marker[] = "MayOS-Profile-v1\n";
    assert(write(fd, marker, sizeof(marker) - 1) == (ssize_t)(sizeof(marker) - 1));
    assert(close(fd) == 0);
}
static void remove_file(const char *root, const char *name) {
    char path[512];
    assert(snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path));
    if (unlink(path)) assert(0 && "test fixture removal failed");
}
static int file_exists(const char *root, const char *name) {
    char path[512];
    assert(snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path));
    return access(path, F_OK) == 0;
}
static void cleanup(const char *root) {
    if (file_exists(root, "desktop-state.tmp"))
        remove_file(root, "desktop-state.tmp");
    if (file_exists(root, "desktop-state-a.bin"))
        remove_file(root, "desktop-state-a.bin");
    if (file_exists(root, "desktop-state-b.bin"))
        remove_file(root, "desktop-state-b.bin");
    remove_file(root, ".mayos-profile-enabled");
    assert(rmdir(root) == 0);
}
static void read_and_expect(const char *message) {
    int valid = 0;
    memset(files, 0, sizeof(files));
    memset(prefs, 0, sizeof(prefs));
    assert(mayos_linux_profile_load(files, prefs, &valid));
    assert(valid == 1);
    assert(strcmp((char *)files[4].data, message) == 0);
    assert(prefs[0] == 0x5a);
}
static void try_crash(const char *stage) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        assert(setenv("MAYOS_PROFILE_TEST_CRASH", stage, 1) == 0);
        set_document("new state after interrupted commit");
        /* Should _exit(97) inside the profile backend at the test-only stage. */
        (void)mayos_linux_profile_save(files, prefs, 1);
        _exit(99);
    }
    int code = 0;
    assert(waitpid(child, &code, 0) == child);
    assert(WIFEXITED(code));
    assert(WEXITSTATUS(code) == 97);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *stage = argv[1];
    char root[] = "/tmp/mayos-ab-fault-XXXXXX";
    assert(mkdtemp(root));
    assert(chmod(root, 0700) == 0);
    root_marker(root);
    assert(setenv("MAYOS_PROFILE_DIR", root, 1) == 0);
    assert(mayos_linux_profile_enabled());
    set_document("last known good");
    assert(mayos_linux_profile_save(files, prefs, 1));
    assert(file_exists(root, "desktop-state-a.bin"));
    read_and_expect("last known good");

    if (!strcmp(stage, "partial_enospc")) {
        assert(setenv("MAYOS_PROFILE_TEST_ERROR", "partial_enospc", 1) == 0);
        set_document("should not persist with full disk");
        assert(!mayos_linux_profile_save(files, prefs, 1));
        assert(unsetenv("MAYOS_PROFILE_TEST_ERROR") == 0);
        assert(!file_exists(root, "desktop-state.tmp"));
        assert(!file_exists(root, "desktop-state-b.bin"));
        read_and_expect("last known good");
    } else {
        const int before_commit =
            !strcmp(stage, "temp_opened") || !strcmp(stage, "temp_written") ||
            !strcmp(stage, "file_synced");
        const int after_rename =
            !strcmp(stage, "renamed") || !strcmp(stage, "directory_synced");
        assert(before_commit || after_rename);
        try_crash(stage);
        /* Process exit is not equivalent to unexpected host power loss.
         * Renames already made in the running kernel remain visible here.
         * Only after directory fsync is the new snapshot declared durable. */
        if (before_commit)
            read_and_expect("last known good");
        else
            read_and_expect("new state after interrupted commit");
        /* A later successful save must remove any abandoned tmp file and
         * commit another valid generation without destroying the backup. */
        set_document("subsequent successful save");
        assert(mayos_linux_profile_save(files, prefs, 1));
        assert(!file_exists(root, "desktop-state.tmp"));
        read_and_expect("subsequent successful save");
    }
    cleanup(root);
    printf("PASS: fault=%s; valid prior snapshot retained / recoverable; no corrupt overwrite\n", stage);
    return 0;
}

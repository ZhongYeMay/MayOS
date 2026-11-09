#include "core.h"
#ifndef MAYOS_LINUX
#include "io.h"
#endif

/* MayFS uses a checksummed fixed-size snapshot on primary IDE master. */
#define FS_SECTORS 512
File files[FS_MAX];
int disk_ready;
#ifndef MAYOS_LINUX
static int ata_wait(int data) {
    for (u32 t = 0; t < 1000000; t++) {
        u8 s = inb(0x1f7);
        if (s == 0 || s == 255)
            return 0;
        if (!(s & 0x80)) {
            if (data >= 0 && (s & 0x21))
                return 0;
            if (data <= 0 || (s & 8))
                return 1;
        }
    }
    return 0;
}
static int sector(u32 lba, void *buf, int write) {
    if (lba >= FS_SECTORS)
        return 0;
    outb(0x1f6, 0xe0 | ((lba >> 24) & 15));
    for (int i = 0; i < 4; i++)
        inb(0x3f6);
    if (!ata_wait(-1))
        return 0;
    outb(0x1f2, 1);
    outb(0x1f3, lba);
    outb(0x1f4, lba >> 8);
    outb(0x1f5, lba >> 16);
    outb(0x1f7, write ? 0x30 : 0x20);
    if (!ata_wait(1))
        return 0;
    u16 *p = buf;
    for (int i = 0; i < 256; i++) {
        if (write)
            outw(0x1f0, p[i]);
        else
            p[i] = inw(0x1f0);
    }
    return ata_wait(0);
}
static u8 fs_image[FS_SECTORS * 512];
static u32 checksum(const u8 *p, u32 n) {
    u32 h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
int fs_sync(void) {
    if (!disk_ready)
        return 0;
    memset(fs_image, 0, sizeof(fs_image));
    memcpy(fs_image, "MAYFS001", 8);
    memcpy(fs_image + 512, files, sizeof(files));
    *(u32 *)(fs_image + 8) = checksum(fs_image + 512, sizeof(files));
    /* Commit the header last; a torn snapshot is refused on the next boot. */
    u8 zero[512];
    memset(zero, 0, 512);
    if (!sector(0, zero, 1))
        return 0;
    for (int i = 1; i < FS_SECTORS; i++)
        if (!sector(i, fs_image + i * 512, 1))
            return 0;
    if (!sector(0, fs_image, 1))
        return 0;
    outb(0x1f7, 0xe7);
    return ata_wait(0);
}
#else
/* Linux desktop files are session RAM objects. Never open raw block devices. */
int fs_sync(void) { return 0; }
#endif
int fs_find(int parent, const char *name) {
    for (int i = 1; i < FS_MAX; i++)
        if (files[i].used && files[i].parent == (u32)parent && !strcmp(files[i].name, name))
            return i;
    return -1;
}
int fs_create(int p, const char *n, int dir) {
    if (p < 0 || p >= FS_MAX || !files[p].used || !files[p].dir || !n[0] || strlen(n) >= 48 ||
        !strcmp(n, ".") || !strcmp(n, ".."))
        return -1;
    for (const char *s = n; *s; s++)
        if (*s == '/' || *s < ' ')
            return -1;
    if (fs_find(p, n) >= 0)
        return -1;
    for (int i = 1; i < FS_MAX; i++)
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(File));
            files[i].used = 1;
            files[i].dir = dir;
            files[i].parent = p;
            copy(files[i].name, n, 48);
            return i;
        }
    return -1;
}
int fs_remove(int i) {
    if (i < 4 || i >= FS_MAX || !files[i].used)
        return 0;
    for (int j = 4; j < FS_MAX; j++)
        if (files[j].used && files[j].parent == (u32)i)
            fs_remove(j);
    memset(&files[i], 0, sizeof(File));
    return 1;
}
int fs_move(int i, int p) {
    if (i < 4 || i >= FS_MAX || !files[i].used || p < 0 || p >= FS_MAX || !files[p].dir ||
        !files[p].used || fs_find(p, files[i].name) >= 0)
        return 0;
    for (int a = p; a; a = files[a].parent)
        if (a == i)
            return 0;
    files[i].parent = p;
    return 1;
}
static void seed(int p, const char *n, const char *s) {
    int i = fs_create(p, n, 0);
    if (i >= 0) {
        copy((char *)files[i].data, s, FILE_CAP);
        files[i].size = strlen(s);
    }
}
void fs_init(void) {
#ifdef MAYOS_LINUX
    disk_ready = 0;
    int valid = 0;
    serial("MayOS Linux: RAM filesystem; no block devices opened\n");
#else
    outb(0x1f6, 0xe0);
    for (int i = 0; i < 4; i++)
        inb(0x3f6);
    disk_ready = ata_wait(-1);
    int valid = 0;
    if (disk_ready) {
        int ok = 1;
        for (int i = 0; i < FS_SECTORS; i++)
            if (!sector(i, fs_image + i * 512, 0)) {
                ok = 0;
                break;
            }
        int signature = 1;
        for (int j = 0; j < 8; j++)
            if (fs_image[j] != "MAYFS001"[j])
                signature = 0;
        if (ok && signature && *(u32 *)(fs_image + 8) == checksum(fs_image + 512, sizeof(files))) {
            memcpy(files, fs_image + 512, sizeof(files));
            valid = 1;
            for (int j = 0; j < FS_MAX; j++)
                if (files[j].used) {
                    File *f = &files[j];
                    if (f->used != 1 || f->dir > 1 || f->parent >= FS_MAX || f->size >= FILE_CAP ||
                        f->name[47] || f->data[f->size])
                        valid = 0;
                }
            for (int j = 0; j < 4; j++)
                if (!files[j].used || !files[j].dir || files[j].parent)
                    valid = 0;
            if (valid)
                for (int j = 1; j < FS_MAX; j++)
                    if (files[j].used) {
                        u32 p = files[j].parent;
                        int count = 0;
                        while (p && count++ < FS_MAX) {
                            if (!files[p].used || !files[p].dir) {
                                valid = 0;
                                break;
                            }
                            p = files[p].parent;
                        }
                        if (count >= FS_MAX)
                            valid = 0;
                    }
        }
        if (!valid) {
            int blank = ok;
            for (int j = 0; j < FS_SECTORS * 512; j++)
                if (fs_image[j]) {
                    blank = 0;
                    break;
                }
            if (!blank) {
                disk_ready = 0;
                serial("MayOS: invalid/unknown disk; preserved, using RAM fallback\n");
            }
        }
    }
#endif

    if (!valid) {
        memset(files, 0, sizeof(files));
        files[0].used = files[0].dir = 1;
        copy(files[0].name, "disk1", 48);
        fs_create(0, "Documents", 1);
        fs_create(0, "Desktop", 1);
        fs_create(0, "Trash", 1);
#ifdef MAYOS_LINUX
        seed(1, "Welcome.txt",
             "Welcome to MayOS Desktop on Linux!\nThis desktop is a real Linux userspace process.\n\n"
             "Try: help, neofetch, ls, cd Documents, cat Welcome.txt\n"
             "Desktop files live in this process RAM and disappear when the session ends.\n"
             "The serial console provides the separate Linux BusyBox shell.\n");
        seed(1, "Roadmap.txt",
             "MayOS 0.4.0-pre41 / Linux desktop preview\n\n"
             "Linux framebuffer display, evdev input and userspace desktop.\n"
             "Future: persistent Linux file storage, networking UI and stronger isolation.\n");
#else
        seed(1, "Welcome.txt",
             "Welcome to MayOS!\nA small operating system with a custom x86 kernel.\n\nTry: help, "
             "neofetch, ls, cd Documents, cat Welcome.txt\nUse write note.txt hello to save a "
             "file.\nFiles persist on the attached MayFS disk.\n");
        seed(1, "Roadmap.txt",
             "MayOS 0.4.0-pre41 / prototype\n\nImplemented: framebuffer desktop, PS/2 input, PIT "
             "timer,\npage allocator, ATA PIO, MayFS, terminal and text editor.\n\nFuture: user "
             "mode, virtual memory, network, USB, SMP.\n");
#endif
        seed(2, "Notes.txt", "Ideas start here.\n");
        fs_sync();
    }
#ifdef MAYOS_LINUX
    serial("MayOS Linux: RAM filesystem initialized\n");
#else
    serial(valid ? "MayOS: MayFS mounted\n" : "MayOS: MayFS initialized (or RAM fallback)\n");
#endif
}

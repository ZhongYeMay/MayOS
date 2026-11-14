#include "core.h"
#include "version.h"
#ifndef MAYOS_LINUX
#include "io.h"
#endif

/* MayFS uses a checksummed fixed-size snapshot on primary IDE master. */
#define FS_SECTORS 512
#define FS_BACKUP_LBA FS_SECTORS
#define FS_TOTAL_SECTORS (FS_SECTORS * 2)
#define PREFS_OFFSET (500U * 512U)
#define PREFS_LENGTH 32U
static u8 saved_prefs[PREFS_LENGTH];
static int prefs_ready;
File files[FS_MAX];
int disk_ready;
#ifndef MAYOS_LINUX
static u32 disk_sectors;
static int backup_enabled;
static int active_slot;
static u32 fs_generation;
#ifdef MAYOS_FS_POWERLOSS_SELFTEST
/* Diagnostic-only compile flag. Never present in the distributable kernel. */
static int fs_powercut_mode;
static void fs_powercut_halt(const char *checkpoint) {
    serial("MayOS: FS POWERLOSS DIAGNOSTIC checkpoint=");
    serial(checkpoint);
    serial("\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
#endif
static u8 backup_image[FS_SECTORS * 512];

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
    if (lba >= FS_TOTAL_SECTORS || lba >= disk_sectors)
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
static u32 ata_capacity(void) {
    u16 identify[256];
    outb(0x1f6, 0xe0);
    for (int i = 0; i < 4; i++) inb(0x3f6);
    outb(0x1f7, 0xec); /* ATA IDENTIFY DEVICE, primary master */
    if (!ata_wait(1)) return 0;
    for (int i = 0; i < 256; i++) identify[i] = inw(0x1f0);
    u32 sectors = (u32)identify[60] | ((u32)identify[61] << 16);
    /* Only LBA28 addressing is implemented in sector(). */
    if (sectors > 0x0fffffffU) sectors = 0x0fffffffU;
    return sectors;
}

static u8 fs_image[FS_SECTORS * 512];
static u32 checksum(const u8 *p, u32 n) {
    u32 h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static int snapshot_blank(const u8 *image) {
    for (u32 i = 0; i < FS_SECTORS * 512; i++)
        if (image[i]) return 0;
    return 1;
}
static int snapshot_owned(const u8 *image) {
    for (int i = 0; i < 8; i++)
        if (image[i] != (u8)"MAYFS001"[i]) return 0;
    return 1;
}
/* Legacy snapshots have no MAY2 generation tag and are generation zero. */
static u32 snapshot_generation(const u8 *image) {
    if (image[12] != 'M' || image[13] != 'A' ||
        image[14] != 'Y' || image[15] != '2')
        return 0;
    u32 value = *(const u32 *)(image + 16);
    return *(const u32 *)(image + 20) == ~value ? value : 0;
}
static int snapshot_valid(const u8 *image) {
    if (!snapshot_owned(image) ||
        *(const u32 *)(image + 8) !=
        checksum(image + 512, sizeof(files)))
        return 0;
    const File *records = (const File *)(image + 512);
    for (int i = 0; i < FS_MAX; i++) {
        const File *f = &records[i];
        if (!f->used) continue;
        if (f->used != 1 || f->dir > 1 ||
            f->parent >= FS_MAX || f->size >= FILE_CAP ||
            f->name[47] || f->data[f->size])
            return 0;
    }
    for (int i = 0; i < 4; i++)
        if (!records[i].used || !records[i].dir || records[i].parent)
            return 0;
    for (int i = 1; i < FS_MAX; i++) {
        if (!records[i].used) continue;
        u32 p = records[i].parent;
        int depth = 0;
        while (p) {
            if (p >= FS_MAX || !records[p].used || !records[p].dir ||
                ++depth >= FS_MAX)
                return 0;
            p = records[p].parent;
        }
    }
    return 1;
}
static int read_snapshot(u32 slot, u8 *destination) {
    u32 base = slot ? FS_BACKUP_LBA : 0;
    for (u32 i = 0; i < FS_SECTORS; i++)
        if (!sector(base + i, destination + i * 512, 0))
            return 0;
    return 1;
}
/* Never invalidate the last valid slot until the other copy's header is
 * written and its ATA FLUSH CACHE command has completed. */
static int write_snapshot(u32 slot) {
    u32 base = slot ? FS_BACKUP_LBA : 0;
    u8 zero[512];
    memset(zero, 0, sizeof(zero));
    if (!sector(base, zero, 1))
        return 0;
    for (u32 i = 1; i < FS_SECTORS; i++) {
        if (!sector(base + i, fs_image + i * 512, 1))
            return 0;
#ifdef MAYOS_FS_POWERLOSS_SELFTEST
        /* Check the on-disk torn state AFTER a real ATA data-sector write. */
        if (slot == 1 && i == 32 && fs_powercut_mode == 1)
            fs_powercut_halt("partial-backup");
#endif
    }
    if (!sector(base, fs_image, 1))
        return 0;
    outb(0x1f7, 0xe7); /* ATA FLUSH CACHE */
    return ata_wait(0);
}
int fs_sync(void) {
    if (!disk_ready)
        return 0;
#ifdef MAYOS_FS_POWERLOSS_SELFTEST
    /* Runtime marker only on an otherwise fully valid private test disk.
     * A test build must not be used for normal users or releases. */
    fs_powercut_mode = fs_find(1, "cut-before.txt") >= 0 ? 1 :
                       fs_find(1, "cut-after.txt") >= 0 ? 2 : 0;
#endif
    memset(fs_image, 0, sizeof(fs_image));
    memcpy(fs_image, "MAYFS001", 8);
    memcpy(fs_image + 512, files, sizeof(files));
    *(u32 *)(fs_image + 8) = checksum(fs_image + 512, sizeof(files));
    memcpy(fs_image + 12, "MAY2", 4);
    u32 generation = fs_generation + 1;
    *(u32 *)(fs_image + 16) = generation;
    *(u32 *)(fs_image + 20) = ~generation;
    if (prefs_ready) {
        memcpy(fs_image + PREFS_OFFSET, "MAYPREF1", 8);
        memcpy(fs_image + PREFS_OFFSET + 12, saved_prefs, PREFS_LENGTH);
        *(u32 *)(fs_image + PREFS_OFFSET + 8) =
            checksum(saved_prefs, PREFS_LENGTH);
    }
    if (!backup_enabled) {
        if (!write_snapshot(0)) return 0;
        fs_generation = generation;
        active_slot = 0;
        return 1;
    }
    /* Write the non-selected slot first: the older valid copy remains intact
     * throughout an interrupted write. After its flush, mirror to the
     * other slot so legacy readers continue to see updated LBA 0 data. */
    u32 first = active_slot ? 0 : 1;
    if (!write_snapshot(first)) return 0;
#ifdef MAYOS_FS_POWERLOSS_SELFTEST
    /* The alternate slot is now valid and ATA FLUSH CACHE completed.
     * Kill QEMU instead of allowing the primary mirror to be updated. */
    if (fs_powercut_mode == 2)
        fs_powercut_halt("backup-committed");
#endif
    fs_generation = generation;
    active_slot = (int)first;
    if (!write_snapshot(1 - first)) return 0;
    active_slot = 0;
    return 1;
}
#else
/* Linux desktop files are session RAM objects. Never open raw block devices. */
int fs_sync(void) { return 0; }
#endif
int fs_prefs_read(u8 *destination, u32 bytes) {
    if (!destination || bytes != PREFS_LENGTH || !prefs_ready)
        return 0;
    memcpy(destination, saved_prefs, PREFS_LENGTH);
    return 1;
}
int fs_prefs_write(const u8 *source, u32 bytes) {
    if (!source || bytes != PREFS_LENGTH)
        return 0;
    memcpy(saved_prefs, source, PREFS_LENGTH);
    prefs_ready = 1;
    return fs_sync();
}
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
    prefs_ready = 0;
    memset(saved_prefs, 0, sizeof(saved_prefs));
#ifdef MAYOS_LINUX
    disk_ready = 0;
    int valid = 0;
    serial("MayOS Linux: RAM filesystem; no block devices opened\n");
#else
    disk_sectors = ata_capacity();
    disk_ready = disk_sectors >= FS_SECTORS;
    backup_enabled = 0;
    active_slot = 0;
    fs_generation = 0;
    int valid = 0;
    int main_valid = 0, backup_valid = 0;
    int main_blank = 0, backup_blank = 1;
    int backup_readable = disk_sectors >= FS_TOTAL_SECTORS;
    if (disk_ready) {
        int main_read = read_snapshot(0, fs_image);
        main_valid = main_read && snapshot_valid(fs_image);
        main_blank = main_read && snapshot_blank(fs_image);
        if (backup_readable) {
            if (!read_snapshot(1, backup_image)) {
                backup_readable = 0;
            } else {
                backup_valid = snapshot_valid(backup_image);
                backup_blank = snapshot_blank(backup_image);
            }
        }
        /* Use the backup region only when it is empty, already a MayFS
         * snapshot, or clearly contains our own damaged header. Never
         * overwrite unknown sectors on a legacy disk. */
        backup_enabled = backup_readable &&
            (backup_valid || backup_blank || snapshot_owned(backup_image));
        if (main_valid || backup_valid) {
            valid = 1;
            u32 main_gen = main_valid ? snapshot_generation(fs_image) : 0;
            u32 backup_gen = backup_valid ?
                snapshot_generation(backup_image) : 0;
            if (backup_valid &&
                (!main_valid || (i32)(backup_gen - main_gen) > 0)) {
                memcpy(fs_image, backup_image, sizeof(fs_image));
                active_slot = 1;
                fs_generation = backup_gen;
                serial("MayOS: recovered MayFS from backup snapshot\n");
            } else {
                active_slot = 0;
                fs_generation = main_gen;
            }
            memcpy(files, fs_image + 512, sizeof(files));
            if (!backup_enabled)
                serial("MayOS: backup region occupied; legacy single-slot mode\n");
        } else if (main_blank && (!backup_readable || backup_blank)) {
            /* A fresh blank disk gets a primary slot before the backup,
             * so an interrupted first-ever format cannot destroy data. */
            active_slot = 1;
        } else {
            disk_ready = 0;
            serial("MayOS: invalid/unknown disk; preserved, using RAM fallback\n");
        }
    }
#endif

    if (valid) {
#ifndef MAYOS_LINUX
        const u8 *record = fs_image + PREFS_OFFSET;
        int magic_ok = 1;
        for (u32 i = 0; i < 8; i++)
            if (record[i] != (u8)"MAYPREF1"[i]) magic_ok = 0;
        if (magic_ok &&
            *(const u32 *)(record + 8) == checksum(record + 12, PREFS_LENGTH)) {
            memcpy(saved_prefs, record + 12, PREFS_LENGTH);
            prefs_ready = 1;
            serial("MayOS: settings restored from MayFS\n");
        }
#endif
    }
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
             "MayOS " MAYOS_VERSION " / Linux desktop preview\n\n"
             "Linux framebuffer display, evdev input and userspace desktop.\n"
             "Future: persistent Linux file storage, networking UI and stronger isolation.\n");
#else
        seed(1, "Welcome.txt",
             "Welcome to MayOS!\nA small operating system with a custom x86 kernel.\n\nTry: help, "
             "neofetch, ls, cd Documents, cat Welcome.txt\nUse write note.txt hello to save a "
             "file.\nFiles persist on the attached MayFS disk.\n");
        seed(1, "Roadmap.txt",
             "MayOS " MAYOS_VERSION " / prototype\n\nImplemented: framebuffer desktop, PS/2 input, PIT "
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

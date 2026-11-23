#include "core.h"
#include "procfs.h"
#include "utf8.h"
#include "version.h"
#ifdef MAYOS_LINUX
#include "../linux/profile_persist.h"
#endif
#ifndef MAYOS_LINUX
#include "io.h"
#include "pci.h"
#include "ahci.h"
#include "nvme.h"
#endif

/* MayFS snapshots live on a dedicated GPT partition or legacy data disk. */
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
static u64 disk_sectors, fs_start, fs_blocks;
static int storage_ahci;
static int storage_nvme;
static u16 ata_command_base = 0x1f0, ata_control_base = 0x3f6;
static int backup_enabled;
static int active_slot;
#ifdef MAYOS_PROCFS
static u64 proc_disk_reads, proc_disk_writes;

void may_proc_io(MayProcSnapshot *snapshot) {
    snapshot->disk_present = disk_ready != 0;
    snapshot->disk_reads = proc_disk_reads;
    snapshot->disk_writes = proc_disk_writes;
}
#endif
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
        u8 s = inb((u16)(ata_command_base + 7));
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
static int ata_sector(u32 lba, void *buf, int write) {
    if (lba >= disk_sectors)
        return 0;
    outb((u16)(ata_command_base + 6), 0xe0 | ((lba >> 24) & 15));
    for (int i = 0; i < 4; i++)
        inb(ata_control_base);
    if (!ata_wait(-1))
        return 0;
    outb((u16)(ata_command_base + 2), 1);
    outb((u16)(ata_command_base + 3), lba);
    outb((u16)(ata_command_base + 4), lba >> 8);
    outb((u16)(ata_command_base + 5), lba >> 16);
    outb((u16)(ata_command_base + 7), write ? 0x30 : 0x20);
    if (!ata_wait(1))
        return 0;
    u16 *p = buf;
    for (int i = 0; i < 256; i++) {
        if (write)
            outw(ata_command_base, p[i]);
        else
            p[i] = inw(ata_command_base);
    }
    return ata_wait(0);
}
static int disk_sector(u64 lba, void *buf, int write) {
    /* Defense in depth: a MayOS Live session must never issue a physical
     * write even if a future caller bypasses the MayFS mounted-disk guard. */
    if (write && platform_ram_only()) return 0;
    if (lba >= disk_sectors) return 0;
    int result;
    if (storage_nvme)
        result = write ? nvme_write_sector(lba, buf) : nvme_read_sector(lba, buf);
    else if (storage_ahci)
        result = write ? ahci_write_sector(lba, buf) : ahci_read_sector(lba, buf);
    else
        result = ata_sector((u32)lba, buf, write);
#ifdef MAYOS_PROCFS
    if (result) {
        if (write) proc_disk_writes++;
        else proc_disk_reads++;
    }
#endif
    return result;
}
static int sector(u32 lba, void *buf, int write) {
    if (lba >= FS_TOTAL_SECTORS || lba >= fs_blocks) return 0;
    return disk_sector(fs_start + lba, buf, write);
}

static u32 gpt_crc(const u8 *bytes, u32 size) {
    u32 crc = ~0u;
    while (size--) {
        crc ^= *bytes++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* The installer creates a conventional GPT with 128-byte entries. Limit
 * reads to 128 entries and refuse ambiguous or overlapping MayFS regions. */
static u8 gpt_entries[128 * 128];
static const u8 mayfs_type[16] = {
    0x4f, 0x59, 0x41, 0x4d, 0x2d, 0x53, 0x53, 0x46,
    0x8c, 0x4d, 0x41, 0x59, 0x46, 0x53, 0x30, 0x31
};

static int bytes_equal(const u8 *a, const u8 *b, u32 size) {
    for (u32 i = 0; i < size; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int gpt_mayfs(u8 *header, const u8 *esp_id) {
    static const u8 esp_type[16] = {
        0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
        0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
    };
    u32 header_size = *(u32 *)(header + 12), header_crc = *(u32 *)(header + 16);
    if (header_size < 92 || header_size > 512 || *(u64 *)(header + 24) != 1)
        return 0;
    *(u32 *)(header + 16) = 0;
    if (gpt_crc(header, header_size) != header_crc) return 0;
    u64 first = *(u64 *)(header + 40), last = *(u64 *)(header + 48);
    u64 table = *(u64 *)(header + 72);
    u32 count = *(u32 *)(header + 80), entry_size = *(u32 *)(header + 84);
    if (entry_size != 128 || !count || count > 128 ||
        table < 2 || table >= disk_sectors || first > last || last >= disk_sectors ||
        *(u64 *)(header + 32) <= last || *(u64 *)(header + 32) >= disk_sectors)
        return 0;
    u32 blocks = (count * 128 + 511) / 512;
    if (table > first || blocks > first - table) return 0;
    for (u32 i = 0; i < blocks; i++)
        if (!disk_sector(table + i, gpt_entries + 512 * i, 0)) return 0;
    if (gpt_crc(gpt_entries, count * 128) != *(u32 *)(header + 88)) return 0;
    u64 start = 0, end = 0;
    unsigned matched_esp = 0;
    for (u32 i = 0; i < count; i++) {
        const u8 *entry = gpt_entries + 128 * i;
        if (esp_id && bytes_equal(entry, esp_type, 16) && bytes_equal(entry + 16, esp_id, 16))
            matched_esp++;
        if (!bytes_equal(entry, mayfs_type, 16)) continue;
        if (start) return 0;
        start = *(const u64 *)(entry + 32);
        end = *(const u64 *)(entry + 40);
        if (start < first || end > last || end < start || end - start + 1 < FS_TOTAL_SECTORS)
            return 0;
    }
    if (!start || (esp_id && matched_esp != 1)) return 0;
    static const u8 empty[16];
    for (u32 i = 0; i < count; i++) {
        const u8 *entry = gpt_entries + 128 * i;
        if (bytes_equal(entry, mayfs_type, 16) || bytes_equal(entry, empty, 16)) continue;
        if (*(const u64 *)(entry + 32) <= end && *(const u64 *)(entry + 40) >= start)
            return 0;
    }
    fs_start = start;
    fs_blocks = end - start + 1;
    return 1;
}

static void select_mayfs_region(const u8 *disk_id, enum MayBootDiskSelector selector) {
    u8 header[512];
    fs_start = fs_blocks = 0;
    if (!disk_sector(0, header, 0)) return;
    int partitioned = header[510] == 0x55 && header[511] == 0xaa &&
                      !bytes_equal(header, (const u8 *)"MAYFS001", 8);
    if (!disk_sector(1, header, 0)) return;
    if (bytes_equal(header, (const u8 *)"EFI PART", 8)) {
        if (selector == MAY_BOOT_DISK_GUID && !bytes_equal(header + 56, disk_id, 16)) return;
        if (gpt_mayfs(header, selector == MAY_BOOT_ESP_GUID ? disk_id : 0) && !disk_id)
            serial("MayOS: MayFS GPT partition selected\n");
        else if (!fs_start && !disk_id) serial("MayOS: no valid MayFS GPT partition\n");
    } else if (!partitioned && !disk_id) {
        fs_blocks = disk_sectors;
    }
}
static u32 ata_capacity(void) {
    u16 identify[256];
    if (!ata_command_base || !ata_control_base) return 0;
    outb((u16)(ata_command_base + 6), 0xe0);
    for (int i = 0; i < 4; i++) inb(ata_control_base);
    outb((u16)(ata_command_base + 7), 0xec); /* ATA IDENTIFY DEVICE, primary master */
    if (!ata_wait(1)) return 0;
    for (int i = 0; i < 256; i++) identify[i] = inw(ata_command_base);
    u32 sectors = (u32)identify[60] | ((u32)identify[61] << 16);
    /* Only LBA28 addressing is implemented in sector(). */
    if (sectors > 0x0fffffffU) sectors = 0x0fffffffU;
    return sectors;
}

enum StorageBackend { BACKEND_NVME, BACKEND_AHCI, BACKEND_ATA, BACKEND_COUNT };

static void set_storage(enum StorageBackend backend, u64 sectors) {
    storage_nvme = backend == BACKEND_NVME;
    storage_ahci = backend == BACKEND_AHCI;
    disk_sectors = sectors;
}

static u64 probe_storage(enum StorageBackend backend) {
    if (backend == BACKEND_NVME) return nvme_init() ? nvme_sector_count() : 0;
    if (backend == BACKEND_AHCI) return ahci_init() ? ahci_sector_count() : 0;
    pci_ide_primary_ports(&ata_command_base, &ata_control_base);
    if (ata_command_base) {
        char port[12];
        number(port, ata_command_base);
        serial("MayOS: ATA primary command port="); serial(port); serial("\n");
    }
    return ata_capacity();
}

static void select_storage(const u8 *disk_id, enum MayBootDiskSelector selector) {
    enum StorageBackend preferred = BACKEND_COUNT;
    u64 selected_start = 0, selected_blocks = 0, selected_sectors = 0;
    for (enum StorageBackend backend = BACKEND_NVME; backend < BACKEND_COUNT; backend++) {
        u64 sectors = probe_storage(backend);
        if (!sectors) continue;
        set_storage(backend, sectors);
        select_mayfs_region(disk_id, selector);
        if (!disk_id) return; /* Original selection for preexisting boot media. */
        if (!fs_start) continue;
        if (preferred != BACKEND_COUNT) {
            fs_start = fs_blocks = 0;
            serial("MayOS: installed disk identity is ambiguous; storage stays in RAM\n");
            return;
        }
        preferred = backend;
        selected_sectors = sectors;
        selected_start = fs_start;
        selected_blocks = fs_blocks;
    }
    if (preferred == BACKEND_COUNT) {
        if (disk_id) serial("MayOS: installed disk not found; storage stays in RAM\n");
        return;
    }
    set_storage(preferred, selected_sectors);
    fs_start = selected_start;
    fs_blocks = selected_blocks;
    serial(preferred == BACKEND_NVME ? "MayOS: installed disk matched on NVMe\n" :
           preferred == BACKEND_AHCI ? "MayOS: installed disk matched on AHCI\n" :
                                       "MayOS: installed disk matched on ATA\n");
    serial("MayOS: MayFS GPT partition selected\n");
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
    if (storage_nvme) return nvme_flush();
    if (storage_ahci) return ahci_flush();
    outb((u16)(ata_command_base + 7), 0xe7); /* ATA FLUSH CACHE */
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
/* Linux only persists to explicitly mounted and marked ext4 data volumes. */
int fs_sync(void) {
    return mayos_linux_profile_save(files, saved_prefs, prefs_ready);
}
#endif
int fs_prefs_read(u8 *destination, u32 bytes) {
    if (!destination || bytes != PREFS_LENGTH || !prefs_ready)
        return 0;
    memcpy(destination, saved_prefs, PREFS_LENGTH);
    return 1;
}
#ifndef MAYOS_LINUX
int fs_installed_setup_pending(void) {
    return disk_ready && fs_start != 0 && !prefs_ready;
}
#endif
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
#ifdef MAYOS_PROCFS
static int proc_mounted;
#endif

void may_vfs_init(void) {
#ifdef MAYOS_PROCFS
    proc_mounted = fs_find(0, "proc") < 0;
    serial(proc_mounted ? "MayOS: Rust procfs mounted read-only at /proc\n"
                        : "MayOS: /proc mount skipped; existing MayFS entry preserved\n");
#endif
}

int may_vfs_info(int node, MayVfsNode *info) {
    if (node >= 0 && node < FS_MAX && files[node].used) {
        const File *file = &files[node];
        *info = (MayVfsNode){file->name, (int)file->parent, (int)file->dir, 0, file->size};
        return 1;
    }
#ifdef MAYOS_PROCFS
    const MayProcEntry *entry = proc_mounted ? mayos_proc_entry(node) : 0;
    if (entry) {
        *info = (MayVfsNode){entry->name, entry->parent, entry->directory, 1, 0};
        return 1;
    }
#endif
    return 0;
}

int may_vfs_next(int directory, int after) {
    MayVfsNode parent;
    if (!may_vfs_info(directory, &parent) || !parent.directory) return -1;
    if (after < 0) after = 0;
    if (directory < FS_MAX && after < FS_MAX) {
        for (int i = after + 1; i < FS_MAX; i++)
            if (files[i].used && files[i].parent == (u32)directory)
                return i;
    }
#ifdef MAYOS_PROCFS
    if (proc_mounted) return mayos_proc_next(directory, after);
#endif
    return -1;
}

int may_vfs_lookup(const char *path, int base) {
    if (*path == '/') base = 0;
    while (*path == '/') path++;
    MayVfsNode info;
    if (!may_vfs_info(base, &info)) return -1;
    while (*path) {
        if (!info.directory) return -1;
        char name[48];
        u32 length = 0;
        while (*path && *path != '/') {
            if (length == sizeof(name) - 1) return -1;
            name[length++] = *path++;
        }
        name[length] = 0;
        if (!strcmp(name, "..")) {
            base = info.parent;
        } else if (strcmp(name, ".")) {
            int child = -1;
            for (int node = may_vfs_next(base, -1); node >= 0; node = may_vfs_next(base, node)) {
                if (may_vfs_info(node, &info) && !strcmp(name, info.name)) {
                    child = node;
                    break;
                }
            }
            base = child;
        }
        if (!may_vfs_info(base, &info) || (*path == '/' && !info.directory)) return -1;
        while (*path == '/') path++;
    }
    return base;
}

int may_vfs_read_only(int node) {
    MayVfsNode info;
    return may_vfs_info(node, &info) && info.read_only;
}

int may_vfs_open(int node, MayVfsReader *reader) {
    MayVfsNode info;
    if (!may_vfs_info(node, &info) || info.directory) return 0;
    reader->offset = 0;
#ifdef MAYOS_PROCFS
    if (info.read_only) {
        int length = mayos_proc_render(node, reader->data, sizeof(reader->data));
        if (length < 0) return 0;
        reader->length = (u32)length;
        return 1;
    }
#endif
    reader->length = info.size < FILE_CAP ? info.size : FILE_CAP;
    memcpy(reader->data, files[node].data, reader->length);
    return 1;
}

u32 may_vfs_read(MayVfsReader *reader, u8 *buffer, u32 capacity) {
    if (reader->offset >= reader->length) return 0;
    u32 remaining = reader->length - reader->offset;
    u32 count = capacity < remaining ? capacity : remaining;
    memcpy(buffer, reader->data + reader->offset, count);
    reader->offset += count;
    return count;
}

static int valid_name(int directory, const char *name) {
    if (!name[0] || strlen(name) >= 48 || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
#ifdef MAYOS_PROCFS
    if (proc_mounted && directory == 0 && !strcmp(name, "proc")) return 0;
#else
    (void)directory;
#endif
    for (const char *s = name; *s;) {
        const char *start = s;
        unsigned cp = may_utf8_next(&s);
        if (cp == '/' || cp < 0x20 || (cp >= 0x7f && cp <= 0x9f) ||
            (cp == 0xfffd && s - start == 1)) return 0;
    }
    return 1;
}

int fs_create(int p, const char *n, int dir) {
    if (p < 0 || p >= FS_MAX || !files[p].used || !files[p].dir || !valid_name(p, n)) return -1;
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
int fs_rename(int node, const char *name) {
    if (node < 4 || node >= FS_MAX || !files[node].used) return 0;
    int directory = (int)files[node].parent;
    if (!valid_name(directory, name)) return 0;
    int duplicate = fs_find(directory, name);
    if (duplicate >= 0 && duplicate != node) return 0;
    copy(files[node].name, name, sizeof(files[node].name));
    return 1;
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
        !files[p].used || !valid_name(p, files[i].name) || fs_find(p, files[i].name) >= 0)
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
    int valid = mayos_linux_profile_load(files, saved_prefs, &prefs_ready);
    disk_ready = mayos_linux_profile_enabled();
    if (valid)
        serial("MayOS Linux: persistent desktop profile restored\n");
    else if (disk_ready)
        serial("MayOS Linux: empty marked profile; initializing first snapshot\n");
    else
        serial("MayOS Linux: RAM filesystem; no persistent volume opened\n");
#else
    u8 selected_disk[16];
    enum MayBootDiskSelector selector = platform_boot_disk(selected_disk);
    if (platform_ram_only() || selector < 0) {
        fs_start = fs_blocks = 0;
        serial(platform_ram_only() ? "MayOS Live: session filesystem runs in RAM; disk writes disabled\n" :
                                     "MayOS: invalid installed disk selector; storage stays in RAM\n");
    } else {
        select_storage(selector ? selected_disk : 0, selector);
    }
    disk_ready = fs_blocks >= FS_SECTORS;
    backup_enabled = 0;
    active_slot = 0;
    fs_generation = 0;
    int valid = 0;
    int main_valid = 0, backup_valid = 0;
    int main_blank = 0, backup_blank = 1;
    int backup_readable = fs_blocks >= FS_TOTAL_SECTORS;
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
    serial(disk_ready ? "MayOS Linux: persistent desktop storage ready\n"
                      : "MayOS Linux: temporary RAM-only desktop storage\n");
#else
    serial(valid ? "MayOS: MayFS mounted\n" : "MayOS: MayFS initialized (or RAM fallback)\n");
#endif
}

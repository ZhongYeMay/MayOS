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
#define LEGACY_SNAPSHOT_BYTES (256u * 1024u)
#define LARGE_SNAPSHOT_BYTES (3584u * 1024u)
#define LARGE_SLOT_A (2u * LEGACY_SNAPSHOT_BYTES)
#define LARGE_SLOT_B (LARGE_SLOT_A + LARGE_SNAPSHOT_BYTES)
#define LARGE_MARKER (LARGE_SLOT_B + LARGE_SNAPSHOT_BYTES)
#define MAX_SECTOR_BYTES 4096u
#define LEGACY_PREFS_OFFSET (500u * 512u)
#define LARGE_PREFS_OFFSET (LARGE_SNAPSHOT_BYTES - MAX_SECTOR_BYTES)
#define PREFS_LENGTH 32U
static u8 saved_prefs[PREFS_LENGTH];
static int prefs_ready;
File files[FS_MAX];
int disk_ready;
#ifndef MAYOS_LINUX
static u64 disk_sectors, fs_start, fs_blocks;
static u32 disk_sector_bytes = 512;
static int large_format, large_marker;
static u32 snapshot_bytes(void) { return large_format ? LARGE_SNAPSHOT_BYTES : LEGACY_SNAPSHOT_BYTES; }
static u32 snapshot_blocks(void) { return snapshot_bytes() / disk_sector_bytes; }
static u32 prefs_offset(void) { return large_format ? LARGE_PREFS_OFFSET : LEGACY_PREFS_OFFSET; }
static u32 slot_block(u32 slot) {
    if (large_format) return (slot ? LARGE_SLOT_B : LARGE_SLOT_A) / disk_sector_bytes;
    return slot ? snapshot_blocks() : 0;
}
static int storage_ahci = -1;
static int disk_read_failed;
static int storage_nvme = -1;
static u16 ata_command_base = 0x1f0, ata_control_base = 0x3f6;
static int backup_enabled;
static int active_slot;
#ifdef MAYOS_PROCFS
static u64 proc_disk_reads, proc_disk_writes, proc_disk_read_bytes, proc_disk_write_bytes;
static int live_disk_detected;

void may_proc_io(MayProcSnapshot *snapshot) {
    snapshot->disk_present = disk_sectors != 0 || live_disk_detected;
    snapshot->disk_mounted = disk_ready != 0;
    snapshot->disk_reads = proc_disk_reads;
    snapshot->disk_writes = proc_disk_writes;
    snapshot->disk_read_bytes = proc_disk_read_bytes;
    snapshot->disk_write_bytes = proc_disk_write_bytes;
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
static u8 backup_image[LARGE_SNAPSHOT_BYTES] __attribute__((aligned(4096)));

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
    if (storage_nvme >= 0)
        result = write ? nvme_write_sector(storage_nvme, lba, buf)
                       : nvme_read_sector(storage_nvme, lba, buf);
    else if (storage_ahci >= 0)
        result = write ? ahci_write_sector(storage_ahci, lba, buf)
                       : ahci_read_sector(storage_ahci, lba, buf);
    else
        result = ata_sector((u32)lba, buf, write);
    if (!write && !result) disk_read_failed = 1;
#ifdef MAYOS_PROCFS
    if (result) {
        if (write) {
            proc_disk_writes++;
            proc_disk_write_bytes += disk_sector_bytes;
        } else {
            proc_disk_reads++;
            proc_disk_read_bytes += disk_sector_bytes;
        }
    }
#endif
    return result;
}
static int sector(u32 lba, void *buf, int write) {
    if (lba >= fs_blocks) return 0;
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
    if (header_size < 92 || header_size > disk_sector_bytes || *(u64 *)(header + 24) != 1)
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
    u32 blocks = (count * 128 + disk_sector_bytes - 1) / disk_sector_bytes;
    if (table > first || blocks > first - table) return 0;
    for (u32 i = 0; i < blocks; i++)
        if (!disk_sector(table + i, gpt_entries + disk_sector_bytes * i, 0)) return 0;
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
        if (start < first || end > last || end < start || end - start + 1 < 2 * LEGACY_SNAPSHOT_BYTES / disk_sector_bytes)
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
    u8 header[MAX_SECTOR_BYTES];
    fs_start = fs_blocks = 0;
    disk_read_failed = 0;
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
    /* The PIO compatibility backend supports only 512-byte logical sectors. */
    if ((identify[106] & 0xd000u) == 0x5000u &&
        (identify[117] != 256 || identify[118]))
        return 0;
    u32 sectors = (u32)identify[60] | ((u32)identify[61] << 16);
    /* Only LBA28 addressing is implemented in sector(). */
    if (sectors > 0x0fffffffU) sectors = 0x0fffffffU;
    return sectors;
}

enum StorageBackend { BACKEND_NVME, BACKEND_AHCI, BACKEND_ATA, BACKEND_COUNT };

static void set_storage(enum StorageBackend backend, int index, u64 sectors) {
    storage_nvme = backend == BACKEND_NVME ? index : -1;
    storage_ahci = backend == BACKEND_AHCI ? index : -1;
    disk_sectors = sectors;
    disk_sector_bytes = backend == BACKEND_NVME ? nvme_sector_bytes(index) :
                        backend == BACKEND_AHCI ? ahci_sector_bytes(index) : 512;
}

static u64 probe_ata(void) {
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
    int preferred_index = 0;
    u64 selected_start = 0, selected_blocks = 0, selected_sectors = 0;
    for (enum StorageBackend backend = BACKEND_NVME; backend < BACKEND_COUNT; backend++) {
        int count = backend == BACKEND_NVME ? nvme_init() :
                    backend == BACKEND_AHCI ? ahci_init() : 1;
        if (count < 0 && disk_id) {
            fs_start = fs_blocks = 0;
            serial(backend == BACKEND_NVME ? "MayOS: NVMe disk scan incomplete; storage stays in RAM\n"
                                           : "MayOS: AHCI disk scan incomplete; storage stays in RAM\n");
            return;
        }
        for (int index = 0; index < count; index++) {
            u64 sectors = backend == BACKEND_NVME ? nvme_sector_count(index) :
                          backend == BACKEND_AHCI ? ahci_sector_count(index) : probe_ata();
            if (!sectors) continue;
            set_storage(backend, index, sectors);
            select_mayfs_region(disk_id, selector);
            if (!disk_id) return; /* Original selection for preexisting boot media. */
            if (disk_read_failed) {
                fs_start = fs_blocks = 0;
                serial("MayOS: disk identity scan incomplete; storage stays in RAM\n");
                return;
            }
            if (!fs_start) continue;
            if (preferred != BACKEND_COUNT) {
                fs_start = fs_blocks = 0;
                serial("MayOS: installed disk identity is ambiguous; storage stays in RAM\n");
                return;
            }
            preferred = backend;
            preferred_index = index;
            selected_sectors = sectors;
            selected_start = fs_start;
            selected_blocks = fs_blocks;
        }
    }
    if (preferred == BACKEND_COUNT) {
        if (disk_id) serial("MayOS: installed disk not found; storage stays in RAM\n");
        return;
    }
    set_storage(preferred, preferred_index, selected_sectors);
    fs_start = selected_start;
    fs_blocks = selected_blocks;
    serial(preferred == BACKEND_NVME ? "MayOS: installed disk matched on NVMe\n" :
           preferred == BACKEND_AHCI ? "MayOS: installed disk matched on AHCI\n" :
                                       "MayOS: installed disk matched on ATA\n");
    serial("MayOS: MayFS GPT partition selected\n");
}

static u8 fs_image[LARGE_SNAPSHOT_BYTES] __attribute__((aligned(4096)));
_Static_assert(512 + sizeof(files) <= LARGE_PREFS_OFFSET, "MayFS file table fits large snapshot");
_Static_assert(512 + sizeof(LegacyFile) * LEGACY_FS_MAX <= LEGACY_PREFS_OFFSET,
               "MayFS file table fits legacy snapshot");
_Static_assert(sizeof(LegacyFile) == 4160, "legacy MayFS file ABI");
static u32 checksum(const u8 *p, u32 n) {
    u32 h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static int snapshot_blank(const u8 *image) {
    for (u32 i = 0; i < snapshot_bytes(); i++)
        if (image[i]) return 0;
    return 1;
}
static int snapshot_owned(const u8 *image) {
    return bytes_equal(image, (const u8 *)(large_format ? "MAYFS002" : "MAYFS001"), 8);
}
/* Legacy snapshots have no MAY2 generation tag and are generation zero. */
static u32 snapshot_generation(const u8 *image) {
    if (image[12] != 'M' || image[13] != 'A' ||
        image[14] != 'Y' || image[15] != '2')
        return 0;
    u32 value = *(const u32 *)(image + 16);
    return *(const u32 *)(image + 20) == ~value ? value : 0;
}
static int snapshot_unpack(const u8 *image) {
    u32 size = large_format ? sizeof(files) : sizeof(LegacyFile) * LEGACY_FS_MAX;
    if (!snapshot_owned(image) || *(const u32 *)(image + 8) != checksum(image + 512, size))
        return 0;
    if (!large_format) return fs_load_legacy_records(files, (const LegacyFile *)(image + 512));
    const File *records = (const File *)(image + 512);
    if (!fs_validate_records(records, FS_MAX, FILE_CAP)) return 0;
    memcpy(files, records, size);
    return 1;
}
static int read_snapshot(u32 slot, u8 *destination) {
    u32 blocks = snapshot_blocks();
    u32 base = slot_block(slot);
    for (u32 i = 0; i < blocks; i++)
        if (!sector(base + i, destination + i * disk_sector_bytes, 0))
            return 0;
    return 1;
}
/* Never invalidate the last valid slot until the other copy's header is
 * written and the drive flush has completed. The first native block is
 * written last, including any file-table bytes it shares on 4Kn. */
static int drive_flush(void) {
    if (storage_nvme >= 0) return nvme_flush(storage_nvme);
    if (storage_ahci >= 0) return ahci_flush(storage_ahci);
    outb((u16)(ata_command_base + 7), 0xe7);
    return ata_wait(0);
}
static int write_snapshot(u32 slot) {
    u32 blocks = snapshot_blocks();
    u32 base = slot_block(slot);
    u8 zero[MAX_SECTOR_BYTES];
    memset(zero, 0, sizeof(zero));
    if (!sector(base, zero, 1))
        return 0;
    for (u32 i = 1; i < blocks; i++) {
        if (!sector(base + i, fs_image + i * disk_sector_bytes, 1))
            return 0;
#ifdef MAYOS_FS_POWERLOSS_SELFTEST
        /* Interrupt after a completed native logical-block write. */
        if (slot == 1 && i * disk_sector_bytes == 32 * 512 && fs_powercut_mode == 1)
            fs_powercut_halt("partial-backup");
#endif
    }
    if (!sector(base, fs_image, 1))
        return 0;
    return drive_flush();
}

static const u8 large_marker_magic[8] = "MAY3DISK";
static int read_large_marker(void) {
    u8 bytes[MAX_SECTOR_BYTES];
    if (!sector(LARGE_MARKER / disk_sector_bytes, bytes, 0)) return -1;
    if (bytes_equal(bytes, large_marker_magic, sizeof(large_marker_magic))) return 1;
    for (u32 i = 0; i < disk_sector_bytes; i++)
        if (bytes[i]) return -1;
    return 0;
}

static int write_large_marker(void) {
    u8 bytes[MAX_SECTOR_BYTES] = {0};
    memcpy(bytes, large_marker_magic, sizeof(large_marker_magic));
    if (!sector(LARGE_MARKER / disk_sector_bytes, bytes, 1) || !drive_flush()) return 0;
    large_marker = 1;
    return 1;
}

static void pack_legacy(LegacyFile *records) {
    for (int i = 0; i < LEGACY_FS_MAX; i++) {
        const File *f = &files[i];
        if (!f->used) continue;
        records[i].used = f->used;
        records[i].dir = f->dir;
        records[i].parent = f->parent;
        records[i].size = f->size;
        memcpy(records[i].name, f->name, sizeof(f->name));
        memcpy(records[i].data, f->data, f->size);
    }
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
    memset(fs_image, 0, snapshot_bytes());
    memcpy(fs_image, large_format ? "MAYFS002" : "MAYFS001", 8);
    u32 size;
    if (large_format) {
        size = sizeof(files);
        memcpy(fs_image + 512, files, size);
    } else {
        size = sizeof(LegacyFile) * LEGACY_FS_MAX;
        if (!fs_validate_records(files, LEGACY_FS_MAX, LEGACY_FILE_CAP)) return 0;
        pack_legacy((LegacyFile *)(fs_image + 512));
    }
    *(u32 *)(fs_image + 8) = checksum(fs_image + 512, size);
    memcpy(fs_image + 12, "MAY2", 4);
    u32 generation = fs_generation + 1;
    *(u32 *)(fs_image + 16) = generation;
    *(u32 *)(fs_image + 20) = ~generation;
    if (prefs_ready) {
        memcpy(fs_image + prefs_offset(), "MAYPREF1", 8);
        memcpy(fs_image + prefs_offset() + 12, saved_prefs, PREFS_LENGTH);
        *(u32 *)(fs_image + prefs_offset() + 8) =
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
    if (large_format && !large_marker && !write_large_marker()) return 0;
#ifdef MAYOS_FS_POWERLOSS_SELFTEST
    /* The alternate slot is now valid and the drive flush completed.
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
    reader->source = reader->data;
#ifdef MAYOS_PROCFS
    if (info.read_only) {
        int length = mayos_proc_render(node, reader->data, sizeof(reader->data));
        if (length < 0) return 0;
        reader->length = (u32)length;
        return 1;
    }
#endif
    reader->length = info.size < FILE_CAP ? info.size : FILE_CAP;
    reader->source = files[node].data;
    return 1;
}

u32 may_vfs_read(MayVfsReader *reader, u8 *buffer, u32 capacity) {
    if (reader->offset >= reader->length) return 0;
    u32 remaining = reader->length - reader->offset;
    u32 count = capacity < remaining ? capacity : remaining;
    memcpy(buffer, reader->source + reader->offset, count);
    reader->offset += count;
    return count;
}

int fs_validate_records(const File *records, u32 count, u32 file_limit) {
    for (unsigned i = 0; i < 4; i++)
        if (records[i].used != 1 || records[i].dir != 1 || records[i].parent || records[i].size)
            return 0;
    for (u32 i = 0; i < count; i++) {
        const File *f = &records[i];
        if (!f->used) continue;
        if (f->used != 1 || f->dir > 1 || f->size >= file_limit ||
            f->name[47] || f->data[f->size] || (f->dir && f->size))
            return 0;
        u32 node = i;
        for (u32 depth = 0; node; depth++) {
            u32 parent = records[node].parent;
            if (parent >= count || !records[parent].used || !records[parent].dir || depth >= count)
                return 0;
            node = parent;
        }
    }
    return 1;
}

int fs_load_legacy_records(File *destination, const LegacyFile *source) {
    memset(destination, 0, sizeof(File) * FS_MAX);
    for (int i = 0; i < LEGACY_FS_MAX; i++) {
        if (!source[i].used) continue;
        if (source[i].size >= LEGACY_FILE_CAP || source[i].data[source[i].size])
            return 0;
        destination[i].used = source[i].used;
        destination[i].dir = source[i].dir;
        destination[i].parent = source[i].parent;
        destination[i].size = source[i].size;
        memcpy(destination[i].name, source[i].name, sizeof(source[i].name));
        memcpy(destination[i].data, source[i].data, source[i].size + 1);
    }
    return fs_validate_records(destination, LEGACY_FS_MAX, LEGACY_FILE_CAP);
}

u32 fs_file_limit(void) {
#ifdef MAYOS_LINUX
    return FILE_CAP - 1;
#else
    return !disk_ready || large_format ? FILE_CAP - 1 : LEGACY_FILE_CAP - 1;
#endif
}

static int fs_entry_limit(void) {
#ifndef MAYOS_LINUX
    if (disk_ready && !large_format) return LEGACY_FS_MAX;
#endif
    return FS_MAX;
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
    for (int i = 1; i < fs_entry_limit(); i++)
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
int fs_put_file(int parent, const char *name, const u8 *data, u32 size) {
    if (size > fs_file_limit() || (size && !data)) return -1;
    int index = fs_create(parent, name, 0);
    if (index < 0) return -1;
    if (size) memcpy(files[index].data, data, size);
    files[index].size = size;
    return index;
}

static u32 package_u32(const u8 *bytes) {
    return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

int fs_import_pack(int parent, const u8 *data, u32 size) {
    if (!data || size < 12 || size > MAYOS_IMPORT_BYTES) return -1;
    for (int i = 0; i < 8; i++)
        if (data[i] != (u8)"MAYPACK1"[i]) return -1;
    u32 count = package_u32(data + 8), offset = 12;
    if (!count || count > MAYOS_IMPORT_FILES) return -1;
    int added[MAYOS_IMPORT_FILES];
    u32 done = 0;
    while (done < count) {
        if (size - offset < 52 || data[offset + 47]) break;
        u32 bytes = package_u32(data + offset + 48);
        const char *name = (const char *)(data + offset);
        offset += 52;
        if (bytes > size - offset) break;
        int file = fs_put_file(parent, name, data + offset, bytes);
        if (file < 0) break;
        added[done++] = file;
        offset += bytes;
    }
    if (done == count && offset == size) return (int)count;
    while (done) fs_remove(added[--done]);
    return -1;
}

static int copy_branch(int source, int destination, const char *name) {
    const File *file = &files[source];
    if (!file->dir) return fs_put_file(destination, name, file->data, file->size);
    int created = fs_create(destination, name, 1);
    if (created < 0) return -1;
    for (int i = 4; i < FS_MAX; i++) {
        if (!files[i].used || files[i].parent != (u32)source) continue;
        if (copy_branch(i, created, files[i].name) < 0) {
            fs_remove(created);
            return -1;
        }
    }
    return created;
}

int fs_copy_tree(int source, int destination) {
    if (source < 4 || source >= FS_MAX || !files[source].used ||
        destination < 0 || destination >= FS_MAX || !files[destination].used ||
        !files[destination].dir)
        return -1;
    for (int i = destination; i; i = files[i].parent)
        if (i == source) return -1;
    char name[48];
    copy(name, files[source].name, sizeof(name));
    if (fs_find(destination, name) >= 0) {
        const char *cursor = name, *end = name;
        while (*cursor) {
            may_utf8_next(&cursor);
            if (cursor - name > 42) break;
            end = cursor;
        }
        copy((char *)end, "-copy", sizeof(name) - (end - name));
    }
    return copy_branch(source, destination, name);
}

static void seed(int p, const char *n, const char *s) {
    int i = fs_create(p, n, 0);
    if (i >= 0) {
        copy((char *)files[i].data, s, FILE_CAP);
        files[i].size = strlen(s);
    }
}
#ifndef MAYOS_LINUX
static int load_snapshot_pair(int allow_backup, int *blank) {
    int main_read = read_snapshot(0, fs_image);
    int main_valid = main_read && snapshot_unpack(fs_image);
    int backup_read = allow_backup && read_snapshot(1, backup_image);
    int backup_valid = backup_read && snapshot_unpack(backup_image);
    *blank = main_read && snapshot_blank(fs_image) &&
             (!allow_backup || (backup_read && snapshot_blank(backup_image)));
    backup_enabled = backup_read &&
        (backup_valid || snapshot_blank(backup_image) || snapshot_owned(backup_image) ||
         (fs_start && main_valid));
    if (!main_valid && !backup_valid) return 0;
    u32 main_gen = main_valid ? snapshot_generation(fs_image) : 0;
    u32 backup_gen = backup_valid ? snapshot_generation(backup_image) : 0;
    if (backup_valid && (!main_valid || (i32)(backup_gen - main_gen) > 0)) {
        memcpy(fs_image, backup_image, snapshot_bytes());
        active_slot = 1;
        fs_generation = backup_gen;
        serial("MayOS: recovered MayFS from backup snapshot\n");
    } else {
        active_slot = 0;
        fs_generation = main_gen;
    }
    return snapshot_unpack(fs_image);
}

static void load_native_prefs(void) {
    const u8 *record = fs_image + prefs_offset();
    if (bytes_equal(record, (const u8 *)"MAYPREF1", 8) &&
        *(const u32 *)(record + 8) == checksum(record + 12, PREFS_LENGTH)) {
        memcpy(saved_prefs, record + 12, PREFS_LENGTH);
        prefs_ready = 1;
        serial("MayOS: settings restored from MayFS\n");
    }
}
#endif

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
    const int ram_only = platform_ram_only();
    if (ram_only || selector < 0) {
        fs_start = fs_blocks = 0;
#ifdef MAYOS_PROCFS
        if (ram_only) {
            /* Identify hardware for /proc and Task Manager without selecting
             * a MayFS target or issuing media reads or writes. */
            nvme_init();
            live_disk_detected = nvme_namespace_count() != 0 ||
                                 ahci_init() > 0 || probe_ata() != 0;
        }
#endif
        serial(ram_only ? "MayOS Live: session filesystem runs in RAM; disk writes disabled\n" :
                                     "MayOS: invalid installed disk selector; storage stays in RAM\n");
    } else {
        select_storage(selector ? selected_disk : 0, selector);
    }
    disk_ready = fs_blocks >= LEGACY_SNAPSHOT_BYTES / disk_sector_bytes;
    backup_enabled = active_slot = large_format = large_marker = 0;
    fs_generation = 0;
    int valid = 0, blank = 0, large_blank = 0;
    int extended_region = disk_ready && fs_start &&
        fs_blocks >= LARGE_MARKER / disk_sector_bytes + 1;
    if (extended_region) {
        int marker = read_large_marker();
        if (marker >= 0) {
            large_format = 1;
            large_marker = marker;
            valid = load_snapshot_pair(1, &large_blank);
            if (valid) load_native_prefs();
        }
        if (!valid && marker > 0) disk_ready = 0;
    }
    if (!valid && disk_ready) {
        large_format = 0;
        valid = load_snapshot_pair(fs_blocks >= 2 * snapshot_blocks(), &blank);
        if (valid) load_native_prefs();
        if (large_blank && (valid || blank)) {
            /* Leave both old snapshots intact. The first new commit uses
             * space reserved in the installed MayFS partition. */
            large_format = backup_enabled = 1;
            active_slot = 1;
        } else if (blank) active_slot = 1;
        else if (!valid) disk_ready = 0;
    }
    if (!valid && !disk_ready && fs_blocks)
        serial("MayOS: invalid/unknown disk; preserved, using RAM fallback\n");
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

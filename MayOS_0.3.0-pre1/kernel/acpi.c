#include "acpi.h"

#define ACPI_MAX_TABLES 64
#define ACPI_MAX_MCFG_RANGES 8
#define PHYSICAL_LIMIT (64ULL << 30)

typedef struct __attribute__((packed)) {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} Rsdp;

typedef struct __attribute__((packed)) {
    char signature[4];
    u32 length;
    u8 revision, checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision, creator_id, creator_revision;
} SdtHeader;

typedef struct {
    u64 base;
    u16 segment;
    u8 start_bus, end_bus;
} McfgRange;

static const SdtHeader *tables[ACPI_MAX_TABLES];
static int table_count;
static McfgRange mcfg_ranges[ACPI_MAX_MCFG_RANGES];
static int mcfg_count;

static int bytes_equal(const char *left, const char *right, int count) {
    for (int i = 0; i < count; i++) if (left[i] != right[i]) return 0;
    return 1;
}

static int checksum_ok(const void *pointer, u32 length) {
    if (!pointer || !length || length > 1024 * 1024) return 0;
    const u8 *bytes = pointer;
    u8 sum = 0;
    for (u32 i = 0; i < length; i++) sum = (u8)(sum + bytes[i]);
    return sum == 0;
}

static const Rsdp *scan_rsdp(uptr start, uptr end) {
    start = (start + 15) & ~(uptr)15;
    for (uptr address = start; address + 36 <= end; address += 16) {
        const Rsdp *rsdp = (const Rsdp *)address;
        if (bytes_equal(rsdp->signature, "RSD PTR ", 8) && checksum_ok(rsdp, 20) &&
            (rsdp->revision < 2 || (rsdp->length >= 36 && rsdp->length <= 4096 &&
                                    checksum_ok(rsdp, rsdp->length))))
            return rsdp;
    }
    return 0;
}

static int valid_sdt(const SdtHeader *header) {
    uptr address = (uptr)header;
    return address >= 0x1000 && address < PHYSICAL_LIMIT &&
           header->length >= sizeof(SdtHeader) && header->length <= 1024 * 1024 &&
           (u64)address + header->length < PHYSICAL_LIMIT && checksum_ok(header, header->length);
}

static void collect_root(const SdtHeader *root, int xsdt) {
    if (!valid_sdt(root)) return;
    u32 entry_size = xsdt ? 8 : 4;
    u32 count = (root->length - sizeof(SdtHeader)) / entry_size;
    const u8 *entries = (const u8 *)root + sizeof(SdtHeader);
    for (u32 i = 0; i < count && table_count < ACPI_MAX_TABLES; i++) {
        u64 address = xsdt ? ((const u64 *)entries)[i] : ((const u32 *)entries)[i];
        if (address >= PHYSICAL_LIMIT) continue;
        const SdtHeader *table = (const SdtHeader *)(uptr)address;
        if (valid_sdt(table)) tables[table_count++] = table;
    }
}

static void parse_mcfg(void) {
    for (int i = 0; i < table_count; i++) {
        const SdtHeader *header = tables[i];
        if (!bytes_equal(header->signature, "MCFG", 4) || header->length < 44) continue;
        const u8 *entry = (const u8 *)header + 44;
        u32 count = (header->length - 44) / 16;
        for (u32 n = 0; n < count && mcfg_count < ACPI_MAX_MCFG_RANGES; n++, entry += 16) {
            McfgRange range = {*(const u64 *)entry, *(const u16 *)(entry + 8),
                               entry[10], entry[11]};
            u64 span = ((u64)range.end_bus - range.start_bus + 1) << 20;
            if (range.segment == 0 && range.start_bus <= range.end_bus &&
                range.base >= 0x100000 && range.base < PHYSICAL_LIMIT &&
                span <= PHYSICAL_LIMIT - range.base)
                mcfg_ranges[mcfg_count++] = range;
        }
    }
}

void acpi_init(void) {
    table_count = mcfg_count = 0;
    u16 ebda_segment;
    __asm__ volatile("movw 0x40e, %0" : "=r"(ebda_segment));
    uptr ebda = (uptr)ebda_segment << 4;
    const Rsdp *rsdp = ebda >= 0x80000 && ebda < 0xa0000 ? scan_rsdp(ebda, ebda + 1024) : 0;
    if (!rsdp) rsdp = scan_rsdp(0xe0000, 0x100000);
    if (!rsdp) {
        serial("MayOS: ACPI RSDP not found; using legacy platform discovery\n");
        return;
    }
    int use_xsdt = rsdp->revision >= 2 && rsdp->xsdt_address && rsdp->xsdt_address < PHYSICAL_LIMIT;
    const SdtHeader *root = (const SdtHeader *)(uptr)(use_xsdt ? rsdp->xsdt_address : rsdp->rsdt_address);
    collect_root(root, use_xsdt);
    parse_mcfg();
    char count[12]; number(count, (u32)table_count);
    serial("MayOS: ACPI tables="); serial(count);
    number(count, (u32)mcfg_count); serial(" MCFG ranges="); serial(count); serial("\n");
}

int acpi_has_table(const char signature[4]) {
    for (int i = 0; i < table_count; i++)
        if (bytes_equal(tables[i]->signature, signature, 4)) return 1;
    return 0;
}

int acpi_has_8042(void) {
    for (int i = 0; i < table_count; i++) {
        const SdtHeader *header = tables[i];
        if (bytes_equal(header->signature, "FACP", 4) &&
            header->revision >= 2 && header->length >= 111) {
            const u8 *fadt = (const u8 *)header;
            return (fadt[109] & 2) != 0;
        }
    }
    /* BIOS and older tables without the IAPC boot architecture flags. */
    return 1;
}

volatile u32 *acpi_pci_config_address(u8 bus, u8 slot, u8 function, u8 offset) {
    for (int i = 0; i < mcfg_count; i++) {
        const McfgRange *range = &mcfg_ranges[i];
        if (bus < range->start_bus || bus > range->end_bus) continue;
        u64 address = range->base + ((u64)(bus - range->start_bus) << 20) +
                      ((u64)slot << 15) + ((u64)function << 12) + (offset & 0xfcu);
        if (address + 4 <= PHYSICAL_LIMIT) return (volatile u32 *)(uptr)address;
    }
    return 0;
}

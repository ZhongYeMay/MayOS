#include "ahci.h"
#include "pci.h"
#include "mmio.h"

#define AHCI_GHC_AE (1u << 31)
#define AHCI_PXCMD_ST (1u << 0)
#define AHCI_PXCMD_FRE (1u << 4)
#define AHCI_PXCMD_FR (1u << 14)
#define AHCI_PXCMD_CR (1u << 15)
#define AHCI_PXIS_TFES (1u << 30)
#define AHCI_SIG_ATA 0x00000101u
#define AHCI_TIMEOUT 10000000u

typedef struct {
    volatile u32 cap, ghc, is, pi, vs, ccc_ctl, ccc_ports, em_loc, em_ctl,
                 cap2, bohc;
    u8 reserved[0xa0 - 0x2c];
    u8 vendor[0x100 - 0xa0];
} AhciHba;

typedef struct {
    volatile u32 clb, clbu, fb, fbu, is, ie, cmd, reserved0, tfd, sig,
                 ssts, sctl, serr, sact, ci, sntf, fbs;
    u32 reserved1[11];
    u32 vendor[4];
} AhciPort;

typedef struct {
    u16 flags;
    u16 prdt_length;
    volatile u32 transferred;
    u32 table, table_upper;
    u32 reserved[4];
} AhciCommandHeader;

typedef struct {
    u32 address, address_upper, reserved;
    u32 byte_count;
} AhciPrdt;

typedef struct {
    u8 fis[64];
    u8 atapi[16];
    u8 reserved[48];
    AhciPrdt prdt[1];
} __attribute__((packed)) AhciCommandTable;

/* Each active port owns its DMA memory, including a bounce buffer which is
 * never returned to the allocator while the controller can still access it. */
typedef struct {
    AhciCommandHeader command_list[32];
    u8 received_fis[256];
    AhciCommandTable command_table __attribute__((aligned(128)));
    u16 sector[256] __attribute__((aligned(512)));
} AhciDma;

typedef struct {
    volatile AhciPort *port;
    AhciDma *dma;
    u64 sectors;
} AhciDisk;

_Static_assert(sizeof(AhciDma) <= 4096, "AHCI DMA must fit in one aligned page");
static AhciDisk disks[PCI_MAX_DEVICES * 32];
static int disk_count;

static void set_address(volatile u32 *low, volatile u32 *high, const void *p) {
    u64 address = (u64)(unsigned long)p;
    *low = (u32)address;
    *high = (u32)(address >> 32);
}

static int wait_clear(volatile u32 *reg, u32 bits) {
    for (u32 timeout = 0; timeout < AHCI_TIMEOUT; timeout++)
        if (!(*reg & bits)) return 1;
    return 0;
}

static int stop_port(volatile AhciPort *port) {
    port->cmd &= ~AHCI_PXCMD_ST;
    if (!wait_clear(&port->cmd, AHCI_PXCMD_CR)) return 0;
    port->cmd &= ~AHCI_PXCMD_FRE;
    return wait_clear(&port->cmd, AHCI_PXCMD_FR);
}

static int start_port(volatile AhciPort *port) {
    if (!wait_clear(&port->cmd, AHCI_PXCMD_CR)) return 0;
    port->cmd |= AHCI_PXCMD_FRE;
    port->cmd |= AHCI_PXCMD_ST;
    return 1;
}

static int issue(AhciDisk *disk, u8 command, u64 lba, u32 bytes, int write) {
    volatile AhciPort *port = disk->port;
    if (!port || !wait_clear(&port->tfd, 0x80u | 0x08u) ||
        !wait_clear(&port->ci, 1u))
        return 0;

    AhciCommandHeader *header = disk->dma->command_list;
    AhciCommandTable *table = &disk->dma->command_table;
    memset(header, 0, sizeof(*header));
    memset(table, 0, sizeof(*table));
    header->flags = 5u | (write ? (1u << 6) : 0u);
    header->prdt_length = bytes ? 1 : 0;
    set_address(&header->table, &header->table_upper, table);
    if (bytes) {
        u64 address = (u64)(uptr)disk->dma->sector;
        table->prdt[0].address = (u32)address;
        table->prdt[0].address_upper = (u32)(address >> 32);
        table->prdt[0].byte_count = bytes - 1u;
    }

    u8 *fis = table->fis;
    fis[0] = 0x27; /* Register Host-to-Device FIS. */
    fis[1] = 1u << 7;
    fis[2] = command;
    fis[4] = (u8)lba;
    fis[5] = (u8)(lba >> 8);
    fis[6] = (u8)(lba >> 16);
    fis[7] = 1u << 6; /* LBA mode. */
    fis[8] = (u8)(lba >> 24);
    fis[9] = (u8)(lba >> 32);
    fis[10] = (u8)(lba >> 40);
    if (bytes) fis[12] = 1; /* One 512-byte sector. */

    port->is = 0xffffffffu;
    __asm__ volatile("mfence" ::: "memory");
    port->ci = 1;
    if (!wait_clear(&port->ci, 1u)) {
        /* An overdue command may still be using this port's DMA page. */
        disk->port = 0;
        return 0;
    }
    __asm__ volatile("mfence" ::: "memory");
    return !(port->is & AHCI_PXIS_TFES) && header->transferred == bytes;
}

static int prepare_port(AhciDisk *disk) {
    volatile AhciPort *port = disk->port;
    if (!stop_port(port)) return 0;
    set_address(&port->clb, &port->clbu, disk->dma->command_list);
    set_address(&port->fb, &port->fbu, disk->dma->received_fis);
    port->ie = 0; /* The driver polls completions. */
    port->serr = 0xffffffffu;
    port->is = 0xffffffffu;
    return start_port(port);
}

static u64 capacity(const u16 *identify) {
    if (!(identify[83] & (1u << 10))) return 0; /* DMA EXT requires LBA48. */
    u16 sector_info = identify[106];
    if ((sector_info & 0xd000u) == 0x5000u &&
        (identify[117] != 256u || identify[118]))
        return 0; /* MayFS currently requires 512-byte logical sectors. */
    return (u64)identify[100] | ((u64)identify[101] << 16) |
           ((u64)identify[102] << 32) | ((u64)identify[103] << 48);
}

int ahci_init(void) {
    int after = -1;
    const PciDevice *device;
    while ((device = pci_find_class(0x01, 0x06, after))) {
        after = (int)(device - pci_device_at(0));
        if (device->prog_if != 0x01) continue;
        u32 bar = device->bar[5];
        if ((bar & 1u) || !(bar & ~0x0fu)) continue;
        volatile AhciHba *hba = mmio_map(bar & ~0x0fu, 0x100u + 32u * 0x80u);
        if (!hba) return -1;
        pci_enable_memory_busmaster(device);
        hba->ghc |= AHCI_GHC_AE;
        if (hba->cap2 & 1u) {
            hba->bohc |= 1u << 1; /* Request ownership from firmware. */
            if (!wait_clear(&hba->bohc, 1u | (1u << 4))) return -1;
        }
        for (u32 index = 0; index < 32; index++) {
            if (!(hba->pi & (1u << index))) continue;
            volatile AhciPort *port = (volatile AhciPort *)
                ((uptr)hba + 0x100u + index * 0x80u);
            u32 status = port->ssts;
            if ((status & 0x0fu) != 3u || ((status >> 8) & 0x0fu) != 1u ||
                port->sig != AHCI_SIG_ATA)
                continue;
            AhciDisk disk = { .port = port, .dma = page_alloc() };
            if (!disk.dma) return -1;
            int identified = prepare_port(&disk) && issue(&disk, 0xec, 0, 512, 0);
            if (identified) disk.sectors = capacity(disk.dma->sector);
            if (!disk.sectors) {
                if (stop_port(port) && !port->ci) page_free(disk.dma);
                if (!identified) return -1;
                continue;
            }
            disks[disk_count++] = disk;
            serial("MayOS: AHCI generic SATA disk ready (LBA48 DMA)\n");
        }
    }
    return disk_count;
}

u64 ahci_sector_count(int index) { return disks[index].sectors; }

int ahci_read_sector(int index, u64 lba, void *buffer) {
    AhciDisk *disk = &disks[index];
    if (lba >= disk->sectors || !issue(disk, 0x25, lba, 512, 0)) return 0;
    memcpy(buffer, disk->dma->sector, 512);
    return 1;
}

int ahci_write_sector(int index, u64 lba, const void *buffer) {
    AhciDisk *disk = &disks[index];
    if (lba >= disk->sectors || !disk->port) return 0;
    memcpy(disk->dma->sector, buffer, 512);
    return issue(disk, 0x35, lba, 512, 1);
}

int ahci_flush(int index) { return issue(&disks[index], 0xea, 0, 0, 0); }

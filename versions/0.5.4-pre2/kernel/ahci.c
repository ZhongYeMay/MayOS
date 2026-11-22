#include "ahci.h"
#include "pci.h"

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

static u8 command_list[1024] __attribute__((aligned(1024)));
static u8 received_fis[256] __attribute__((aligned(256)));
static AhciCommandTable command_table __attribute__((aligned(128)));
static u16 identify_data[256] __attribute__((aligned(512)));
static volatile AhciPort *active_port;
static u64 active_sectors;

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

static int issue(u8 command, u64 lba, void *buffer, u32 bytes, int write) {
    volatile AhciPort *port = active_port;
    if (!port) return 0;
    for (u32 timeout = 0; timeout < AHCI_TIMEOUT; timeout++) {
        if (!(port->tfd & (0x80u | 0x08u))) break;
        if (timeout + 1 == AHCI_TIMEOUT) return 0;
    }

    AhciCommandHeader *header = (AhciCommandHeader *)command_list;
    memset(header, 0, sizeof(*header));
    memset(&command_table, 0, sizeof(command_table));
    header->flags = 5u | (write ? (1u << 6) : 0u);
    header->prdt_length = bytes ? 1 : 0;
    set_address(&header->table, &header->table_upper, &command_table);
    if (bytes) {
        u64 address = (u64)(unsigned long)buffer;
        command_table.prdt[0].address = (u32)address;
        command_table.prdt[0].address_upper = (u32)(address >> 32);
        command_table.prdt[0].byte_count = (bytes - 1u) | (1u << 31);
    }

    u8 *fis = command_table.fis;
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
    for (u32 timeout = 0; timeout < AHCI_TIMEOUT; timeout++) {
        if (!(port->ci & 1u))
            return !(port->is & AHCI_PXIS_TFES);
    }
    return 0;
}

static int prepare_port(volatile AhciPort *port) {
    if (!stop_port(port)) return 0;
    memset(command_list, 0, sizeof(command_list));
    memset(received_fis, 0, sizeof(received_fis));
    set_address(&port->clb, &port->clbu, command_list);
    set_address(&port->fb, &port->fbu, received_fis);
    port->serr = 0xffffffffu;
    port->is = 0xffffffffu;
    return start_port(port);
}

int ahci_init(void) {
    active_port = 0;
    active_sectors = 0;
    int after = -1;
    const PciDevice *device;
    while ((device = pci_find_class(0x01, 0x06, after))) {
        after = (int)(device - pci_device_at(0));
        if (device->prog_if != 0x01) continue;
        u32 bar = device->bar[5];
        if ((bar & 1u) || !(bar & ~0x0fu)) continue;
        pci_enable_memory_busmaster(device);
        volatile AhciHba *hba = (volatile AhciHba *)(unsigned long)(bar & ~0x0fu);
        hba->ghc |= AHCI_GHC_AE;
        if (hba->cap2 & 1u) {
            hba->bohc |= 1u << 1; /* Request ownership from firmware. */
            for (u32 timeout = 0; timeout < AHCI_TIMEOUT; timeout++)
                if (!(hba->bohc & 1u)) break;
        }
        for (int index = 0; index < 32; index++) {
            if (!(hba->pi & (1u << index))) continue;
            volatile AhciPort *port = (volatile AhciPort *)
                ((unsigned long)hba + 0x100u + (u32)index * 0x80u);
            u32 status = port->ssts;
            if ((status & 0x0fu) != 3u || ((status >> 8) & 0x0fu) != 1u ||
                port->sig != AHCI_SIG_ATA)
                continue;
            if (!prepare_port(port)) continue;
            active_port = port;
            if (!issue(0xec, 0, identify_data, 512, 0)) {
                active_port = 0;
                continue;
            }
            if (identify_data[83] & (1u << 10)) {
                active_sectors = (u64)identify_data[100] |
                    ((u64)identify_data[101] << 16) |
                    ((u64)identify_data[102] << 32) |
                    ((u64)identify_data[103] << 48);
            } else {
                active_sectors = (u32)identify_data[60] |
                    ((u32)identify_data[61] << 16);
            }
            if (active_sectors) {
                serial("MayOS: AHCI generic SATA disk ready (LBA48 DMA)\n");
                return 1;
            }
            active_port = 0;
        }
    }
    serial("MayOS: AHCI unavailable; using ATA compatibility fallback\n");
    return 0;
}

u64 ahci_sector_count(void) { return active_sectors; }

int ahci_read_sector(u64 lba, void *buffer) {
    return lba < active_sectors && issue(0x25, lba, buffer, 512, 0);
}

int ahci_write_sector(u64 lba, const void *buffer) {
    return lba < active_sectors && issue(0x35, lba, (void *)buffer, 512, 1);
}

int ahci_flush(void) { return issue(0xea, 0, 0, 0, 0); }

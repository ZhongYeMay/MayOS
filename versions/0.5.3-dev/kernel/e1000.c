#include "core.h"
#include "pci.h"
#include "e1000.h"

#define RX_COUNT 32
#define TX_COUNT 8
#define FRAME_SIZE 2048
#define E1000_TIMEOUT 1000000u

typedef struct {
    u64 address;
    u16 length, checksum;
    u8 status, errors;
    u16 special;
} E1000RxDescriptor;

typedef struct {
    u64 address;
    u16 length;
    u8 checksum_offset, command, status, checksum_start;
    u16 special;
} E1000TxDescriptor;

static E1000RxDescriptor rx_ring[RX_COUNT] __attribute__((aligned(128)));
static E1000TxDescriptor tx_ring[TX_COUNT] __attribute__((aligned(128)));
static u8 rx_buffers[RX_COUNT][FRAME_SIZE] __attribute__((aligned(4096)));
static u8 tx_buffers[TX_COUNT][FRAME_SIZE] __attribute__((aligned(4096)));
static volatile u8 *mmio;
static u8 mac[6];
static u32 rx_index, tx_index;

static u32 read_reg(u32 offset) { return *(volatile u32 *)(mmio + offset); }
static void write_reg(u32 offset, u32 value) { *(volatile u32 *)(mmio + offset) = value; }

static int supported(const PciDevice *device) {
    if (device->vendor_id != 0x8086 || device->class_code != 0x02) return 0;
    /* Register-compatible 8254x/8257x/I21x controllers. Unknown Intel
       network devices are left untouched instead of assuming compatibility. */
    switch (device->device_id) {
    case 0x100e: case 0x100f: case 0x1010: case 0x107c: case 0x109a:
    case 0x10b9: case 0x10bc: case 0x10bd: case 0x10c9: case 0x10d3:
    case 0x10ea: case 0x1502: case 0x1503: case 0x153a: case 0x153b:
    case 0x15b8: case 0x15d8:
        return 1;
    default: return 0;
    }
}

int e1000_init(void) {
    mmio = 0;
    const PciDevice *device = 0;
    for (int i = 0; i < pci_device_count(); i++) {
        const PciDevice *candidate = pci_device_at(i);
        if (supported(candidate)) { device = candidate; break; }
    }
    if (!device) return 0;
    u32 bar = device->bar[0];
    if ((bar & 1u) || !(bar & ~0x0fu)) return 0;
    u64 address = bar & ~0x0fu;
    if ((bar & 6u) == 4u) address |= (u64)device->bar[1] << 32;
    if (address >= (64ULL << 30)) return 0;
    pci_enable_memory_busmaster(device);
    mmio = (volatile u8 *)(unsigned long)address;
    write_reg(0x00d8, 0xffffffffu); /* Mask interrupts; polling first. */
    (void)read_reg(0x00c0);
    u32 ral = read_reg(0x5400), rah = read_reg(0x5404);
    if (!(rah & (1u << 31))) { mmio = 0; return 0; }
    for (int i = 0; i < 4; i++) mac[i] = (u8)(ral >> (i * 8));
    mac[4] = (u8)rah; mac[5] = (u8)(rah >> 8);

    memset(rx_ring, 0, sizeof(rx_ring)); memset(tx_ring, 0, sizeof(tx_ring));
    for (u32 i = 0; i < RX_COUNT; i++)
        rx_ring[i].address = (u64)(unsigned long)rx_buffers[i];
    for (u32 i = 0; i < TX_COUNT; i++) {
        tx_ring[i].address = (u64)(unsigned long)tx_buffers[i];
        tx_ring[i].status = 1;
    }
    u64 rx = (u64)(unsigned long)rx_ring, tx = (u64)(unsigned long)tx_ring;
    write_reg(0x2800, (u32)rx); write_reg(0x2804, (u32)(rx >> 32));
    write_reg(0x2808, sizeof(rx_ring)); write_reg(0x2810, 0);
    write_reg(0x2818, RX_COUNT - 1); rx_index = 0;
    write_reg(0x3800, (u32)tx); write_reg(0x3804, (u32)(tx >> 32));
    write_reg(0x3808, sizeof(tx_ring)); write_reg(0x3810, 0);
    write_reg(0x3818, 0); tx_index = 0;
    write_reg(0x0100, (1u << 1) | (1u << 15) | (1u << 26));
    write_reg(0x0400, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12));
    write_reg(0x0410, 10u | (8u << 10) | (6u << 20));
    write_reg(0x0000, read_reg(0x0000) | (1u << 6)); /* Set Link Up. */
    serial("MayOS: E1000 generic Ethernet rings ready\n");
    return 1;
}

int e1000_link_up(void) { return mmio && (read_reg(0x0008) & 2u); }
const u8 *e1000_mac_address(void) { return mmio ? mac : 0; }

int e1000_send(const void *frame, u32 length) {
    if (!mmio || !frame || length < 14 || length > 1518) return 0;
    volatile E1000TxDescriptor *descriptor = &tx_ring[tx_index];
    for (u32 timeout = 0; !(descriptor->status & 1u); timeout++)
        if (timeout == E1000_TIMEOUT) return 0;
    memcpy(tx_buffers[tx_index], frame, length);
    descriptor->length = (u16)length; descriptor->status = 0;
    descriptor->command = (1u << 0) | (1u << 1) | (1u << 3);
    __asm__ volatile("mfence" ::: "memory");
    tx_index = (tx_index + 1u) % TX_COUNT;
    write_reg(0x3818, tx_index);
    return 1;
}

int e1000_receive(void *frame, u32 capacity) {
    if (!mmio || !frame) return 0;
    volatile E1000RxDescriptor *descriptor = &rx_ring[rx_index];
    if (!(descriptor->status & 1u)) return 0;
    u32 length = descriptor->length;
    if ((descriptor->status & 2u) && !descriptor->errors &&
        length >= 14u && length <= FRAME_SIZE && length <= capacity)
        memcpy(frame, rx_buffers[rx_index], length);
    else
        length = 0;
    descriptor->status = 0;
    __asm__ volatile("mfence" ::: "memory");
    write_reg(0x2818, rx_index);
    rx_index = (rx_index + 1u) % RX_COUNT;
    return (int)length;
}

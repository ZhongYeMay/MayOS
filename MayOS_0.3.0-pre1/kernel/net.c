#include "net.h"
#include "io.h"

/* The RTL8139 is deliberately opt-in by PCI device ID. Unrecognized network
 * hardware remains untouched. A single polling driver avoids changing PIC/IDT.
 * MayKernel's boot64.S maps early physical RAM 1:1; the statically allocated
 * DMA buffers and PCI BAR are verified below to fit their 32-bit hardware ABI.
 *
 * References: RTL8139 datasheet (CONFIG1, RBSTART, CMD, CAPR, RCR, TSD/TSAD)
 * and OSDev RTL8139 bring-up guide. This is QEMU experimental code, NOT a
 * full Ethernet/IP driver or production-grade hardware DMA isolation. */
#define RTL_VENDOR 0x10ecu
#define RTL_DEVICE 0x8139u
#define RTL_RING_BYTES 8192u
#define RTL_RX_ALLOC (RTL_RING_BYTES + 16u + 2048u)
#define RTL_TX_ALLOC 1536u

enum {
    RTL_TSD0 = 0x10, RTL_TSAD0 = 0x20,
    RTL_RBSTART = 0x30, RTL_CMD = 0x37, RTL_CAPR = 0x38,
    RTL_IMR = 0x3c, RTL_ISR = 0x3e, RTL_RCR = 0x44,
    RTL_CONFIG1 = 0x52
};
static MayNetInfo nic;
static u32 rx_cursor, next_tx;
static u8 rx_ring[RTL_RX_ALLOC] __attribute__((aligned(256)));
static u8 tx_frames[4][RTL_TX_ALLOC] __attribute__((aligned(16)));

static u32 pci_read(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = 0x80000000u |
        ((u32)bus << 16) | ((u32)slot << 11) |
        ((u32)func << 8) | ((u32)offset & 0xfcu);
    outl(0xcf8, address);
    return inl(0xcfc);
}
static void pci_write(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = 0x80000000u |
        ((u32)bus << 16) | ((u32)slot << 11) |
        ((u32)func << 8) | ((u32)offset & 0xfcu);
    outl(0xcf8, address);
    outl(0xcfc, value);
}
static u8 ring_byte(u32 address) {
    return rx_ring[address & (RTL_RING_BYTES - 1u)];
}
static int address_32bit(const void *pointer) {
    uptr value = (uptr)pointer;
    return value != 0 && value <= 0xffffffffu;
}
static int rtl_setup(u8 bus, u8 slot, u8 func) {
    u32 bar = pci_read(bus, slot, func, 0x10);
    if (!(bar & 1u)) {
        serial("MayOS: RTL8139 PCI BAR0 is not I/O; refusing init\n");
        return 0;
    }
    u32 base = bar & ~3u;
    if (!base || base > 0x10000u - 0x60u ||
        !address_32bit(rx_ring) ||
        !address_32bit(&rx_ring[RTL_RX_ALLOC - 1]) ||
        !address_32bit(tx_frames) ||
        !address_32bit(&tx_frames[3][RTL_TX_ALLOC - 1])) {
        serial("MayOS: RTL8139 invalid I/O BAR or DMA memory\n");
        return 0;
    }
    u32 old = pci_read(bus, slot, func, 0x04);
    pci_write(bus, slot, func, 0x04, old | 0x5u); /* PCI I/O + bus master */
    if ((pci_read(bus, slot, func, 0x04) & 0x5u) != 0x5u) {
        serial("MayOS: RTL8139 PCI bus mastering unavailable\n");
        return 0;
    }

    u16 io = (u16)base;
    outb(io + RTL_CONFIG1, 0x00);
    outb(io + RTL_CMD, 0x10); /* bounded software reset */
    u32 remaining = 100000u;
    while ((inb(io + RTL_CMD) & 0x10u) && remaining) remaining--;
    if (!remaining) {
        serial("MayOS: RTL8139 reset timeout\n");
        return 0;
    }

    memset(rx_ring, 0, sizeof(rx_ring));
    memset(tx_frames, 0, sizeof(tx_frames));
    nic.io_base = io;
    for (int i = 0; i < 6; i++) nic.mac[i] = inb(io + i);
    int any = 0, all_ff = 1;
    for (int i = 0; i < 6; i++) {
        if (nic.mac[i]) any = 1;
        if (nic.mac[i] != 0xff) all_ff = 0;
    }
    if (!any || all_ff || (nic.mac[0] & 1u)) {
        serial("MayOS: RTL8139 invalid hardware MAC\n");
        return 0;
    }
    rx_cursor = next_tx = 0;
    outw(io + RTL_IMR, 0); /* polling only; don't fire unhandled IRQs */
    outw(io + RTL_ISR, 0xffffu);
    outl(io + RTL_RBSTART, (u32)(uptr)rx_ring);
    /* Accept physical-MAC and broadcast frames only; 8 KiB ring, WRAP.
     * Reserve 16+2048 trailing bytes for DMA packets crossing the ring. */
    outl(io + RTL_RCR, (1u << 7) | (1u << 3) | (1u << 1));
    for (u32 i = 0; i < 4; i++)
        outl(io + RTL_TSAD0 + i * 4u, (u32)(uptr)tx_frames[i]);
    outb(io + RTL_CMD, 0x0c); /* receiver + transmitter */
    nic.present = 1;
    serial("MayOS: RTL8139 native PCI Ethernet ready (polling, no IP stack)\n");
    return 1;
}
void may_net_init(void) {
    memset(&nic, 0, sizeof(nic));
    for (u32 bus = 0; bus < 256u; bus++)
        for (u32 slot = 0; slot < 32u; slot++) {
            u32 id0 = pci_read((u8)bus, (u8)slot, 0, 0);
            if ((id0 & 0xffffu) == 0xffffu) continue;
            u32 functions = (pci_read((u8)bus, (u8)slot, 0, 0x0c) &
                             0x00800000u) ? 8u : 1u;
            for (u32 function = 0; function < functions; function++) {
                u32 id = function ? pci_read((u8)bus, (u8)slot,
                                              (u8)function, 0) : id0;
                if ((id & 0xffffu) != RTL_VENDOR ||
                    (id >> 16) != RTL_DEVICE) continue;
                if (rtl_setup((u8)bus, (u8)slot, (u8)function)) return;
            }
        }
    serial("MayOS: no supported RTL8139 PCI Ethernet adapter\n");
}
const MayNetInfo *may_net_info(void) { return &nic; }

/* Only accept an ARP response corresponding to the built-in QEMU gateway
 * diagnostic. Don't treat arbitrary LAN packets as successful probes. */
static void receive_frame(u32 offset, u32 frame_len) {
    if (frame_len < 42u) return;
    if (ring_byte(offset + 12u) != 0x08 ||
        ring_byte(offset + 13u) != 0x06) return;
    if (ring_byte(offset + 14u) != 0x00 ||
        ring_byte(offset + 15u) != 0x01 ||
        ring_byte(offset + 16u) != 0x08 ||
        ring_byte(offset + 17u) != 0x00 ||
        ring_byte(offset + 18u) != 6 ||
        ring_byte(offset + 19u) != 4 ||
        ring_byte(offset + 20u) != 0 ||
        ring_byte(offset + 21u) != 2) return;
    const u8 gateway[4] = {10, 0, 2, 2};
    const u8 client[4] = {10, 0, 2, 15};
    for (int i = 0; i < 6; i++) {
        if (ring_byte(offset + (u32)i) != nic.mac[i] ||
            ring_byte(offset + 32u + (u32)i) != nic.mac[i]) return;
    }
    for (int i = 0; i < 4; i++) {
        if (ring_byte(offset + 28u + (u32)i) != gateway[i] ||
            ring_byte(offset + 38u + (u32)i) != client[i]) return;
    }
    if (!nic.gateway_arp_reply) {
        nic.gateway_arp_reply = 1;
        serial("MayOS: RTL8139 ARP reply received from QEMU gateway 10.0.2.2\n");
    }
}
void may_net_poll(void) {
    if (!nic.present) return;
    const u16 io = nic.io_base;
    /* ACK masked hardware events in polling mode; no IRQ handler required. */
    u16 events = inw(io + RTL_ISR);
    if (events && events != 0xffffu) outw(io + RTL_ISR, events);
    for (u32 i = 0; i < 16u && !(inb(io + RTL_CMD) & 1u); i++) {
        u16 status = (u16)ring_byte(rx_cursor) |
                     ((u16)ring_byte(rx_cursor + 1u) << 8);
        u32 length = (u32)ring_byte(rx_cursor + 2u) |
                     ((u32)ring_byte(rx_cursor + 3u) << 8);
        if (!(status & 1u) || length < 8u || length > 2048u) {
            nic.rx_errors++;
            serial("MayOS: RTL8139 RX malformed descriptor; stopping poll\n");
            /* Do not advance CAPR using an untrusted DMA length. */
            break;
        }
        receive_frame(rx_cursor + 4u, length - 4u /* hardware CRC */);
        nic.packets_rx++;
        rx_cursor = (rx_cursor + 4u + length + 3u) &
                    ~(u32)3u;
        rx_cursor &= (RTL_RING_BYTES - 1u);
        outw(io + RTL_CAPR, (u16)(rx_cursor - 16u));
    }
}
int may_net_qemu_arp_probe(void) {
    if (!nic.present) return 0;
    may_net_poll();
    const u16 io = nic.io_base;
    u32 index = next_tx;
    /* The OWN flag must be 1: DMA from the previous frame is finished. */
    if (!(inl(io + RTL_TSD0 + index * 4u) & (1u << 13))) return 0;
    u8 *packet = tx_frames[index];
    memset(packet, 0, 60u);
    for (int i = 0; i < 6; i++) {
        packet[i] = 0xff;         /* Ethernet broadcast */
        packet[6 + i] = nic.mac[i];
        packet[22 + i] = nic.mac[i]; /* ARP sender hardware address */
    }
    packet[12] = 0x08; packet[13] = 0x06; /* Ethernet ARP */
    packet[14] = 0; packet[15] = 1;      /* Ethernet hardware type */
    packet[16] = 0x08; packet[17] = 0;   /* IPv4 protocol */
    packet[18] = 6; packet[19] = 4;
    packet[20] = 0; packet[21] = 1;      /* ARP request */
    packet[28] = 10; packet[29] = 0; packet[30] = 2; packet[31] = 15;
    packet[38] = 10; packet[39] = 0; packet[40] = 2; packet[41] = 2;
    nic.gateway_arp_reply = 0;
    /* A serial fence ensures the DMA-visible buffer is populated first. */
    __asm__ volatile("" ::: "memory");
    outl(io + RTL_TSD0 + index * 4u, 60u);
    next_tx = (index + 1u) & 3u;
    nic.packets_tx++;
    serial("MayOS: RTL8139 QEMU ARP probe sent to 10.0.2.2\n");
    return 1;
}

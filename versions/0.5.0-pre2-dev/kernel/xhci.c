#include "core.h"
#include "pci.h"
#include "xhci.h"

#define XHCI_TIMEOUT 10000000u
#define XHCI_RING_TRBS 256
#define XHCI_MAX_SCRATCHPADS 32

typedef struct {
    u64 address;
    u32 size;
    u32 reserved;
} XhciErstEntry;

typedef struct {
    u64 parameter;
    u32 status;
    u32 control;
} XhciTrb;

static u64 dcbaa[256] __attribute__((aligned(64)));
static XhciTrb command_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static XhciTrb event_ring[XHCI_RING_TRBS] __attribute__((aligned(64)));
static XhciErstEntry erst[1] __attribute__((aligned(64)));
static u64 scratchpad_array[XHCI_MAX_SCRATCHPADS] __attribute__((aligned(64)));
static u8 scratchpads[XHCI_MAX_SCRATCHPADS][4096] __attribute__((aligned(4096)));
static int controllers_ready, ports_connected;

static u32 read32(volatile u8 *base, u32 offset) {
    return *(volatile u32 *)(base + offset);
}

static void write32(volatile u8 *base, u32 offset, u32 value) {
    *(volatile u32 *)(base + offset) = value;
}

static void write64(volatile u8 *base, u32 offset, u64 value) {
    *(volatile u64 *)(base + offset) = value;
}

static int wait32(volatile u8 *base, u32 offset, u32 mask, int set) {
    for (u32 timeout = 0; timeout < XHCI_TIMEOUT; timeout++) {
        int present = (read32(base, offset) & mask) != 0;
        if (present == set) return 1;
    }
    return 0;
}

static int firmware_handoff(volatile u8 *capability, u32 hccparams1) {
    u32 extended = (hccparams1 >> 16) * 4u;
    for (int guard = 0; extended && guard < 256; guard++) {
        u32 value = read32(capability, extended);
        u8 id = (u8)value;
        u8 next = (u8)(value >> 8);
        if (id == 1) {
            write32(capability, extended, value | (1u << 24));
            if (!wait32(capability, extended, 1u << 16, 0)) {
                serial("MayOS: xHCI firmware ownership timeout\n");
                return 0;
            }
        }
        if (!next) break;
        extended += (u32)next * 4u;
    }
    return 1;
}

static int initialize_controller(const PciDevice *device) {
    u32 bar0 = device->bar[0];
    if ((bar0 & 1u) || !(bar0 & ~0x0fu)) return 0;
    u64 address = bar0 & ~0x0fu;
    if ((bar0 & 6u) == 4u) address |= (u64)device->bar[1] << 32;
    if (address >= (64ULL << 30)) {
        serial("MayOS: xHCI MMIO lies outside the mapped physical range\n");
        return 0;
    }
    pci_enable_memory_busmaster(device);
    volatile u8 *capability = (volatile u8 *)(unsigned long)address;
    u8 cap_length = *(volatile u8 *)capability;
    if (cap_length < 0x20) return 0;
    u32 hcsparams1 = read32(capability, 0x04);
    u32 hcsparams2 = read32(capability, 0x08);
    u32 hccparams1 = read32(capability, 0x10);
    u32 max_slots = hcsparams1 & 0xffu;
    u32 max_ports = hcsparams1 >> 24;
    if (!max_slots || !max_ports || max_slots > 255) return 0;
    if (!firmware_handoff(capability, hccparams1)) return 0;

    volatile u8 *operational = capability + cap_length;
    if (!(read32(operational, 0x08) & 1u)) {
        serial("MayOS: xHCI controller does not support 4 KiB pages\n");
        return 0;
    }
    u32 command = read32(operational, 0x00) & ~1u;
    write32(operational, 0x00, command);
    if (!wait32(operational, 0x04, 1u, 1)) return 0; /* HCHalted */
    write32(operational, 0x00, command | (1u << 1)); /* Host Controller Reset */
    if (!wait32(operational, 0x00, 1u << 1, 0) ||
        !wait32(operational, 0x04, 1u << 11, 0))
        return 0;

    memset(dcbaa, 0, sizeof(dcbaa));
    memset(command_ring, 0, sizeof(command_ring));
    memset(event_ring, 0, sizeof(event_ring));
    memset(erst, 0, sizeof(erst));
    command_ring[XHCI_RING_TRBS - 1].parameter =
        (u64)(unsigned long)command_ring;
    command_ring[XHCI_RING_TRBS - 1].control =
        (6u << 10) | (1u << 1) | 1u; /* Link TRB, toggle cycle, cycle=1. */

    u32 scratchpad_count = (((hcsparams2 >> 21) & 0x1fu) << 5) |
                            ((hcsparams2 >> 27) & 0x1fu);
    if (scratchpad_count > XHCI_MAX_SCRATCHPADS) {
        serial("MayOS: xHCI requires too many scratchpad buffers\n");
        return 0;
    }
    if (scratchpad_count) {
        for (u32 i = 0; i < scratchpad_count; i++)
            scratchpad_array[i] = (u64)(unsigned long)scratchpads[i];
        dcbaa[0] = (u64)(unsigned long)scratchpad_array;
    }
    write64(operational, 0x30, (u64)(unsigned long)dcbaa);
    write64(operational, 0x18, (u64)(unsigned long)command_ring | 1u);
    write32(operational, 0x38, max_slots);

    u32 runtime_offset = read32(capability, 0x18) & ~0x1fu;
    volatile u8 *interrupter = capability + runtime_offset + 0x20;
    erst[0].address = (u64)(unsigned long)event_ring;
    erst[0].size = XHCI_RING_TRBS;
    write32(interrupter, 0x08, 1);
    write64(interrupter, 0x10, (u64)(unsigned long)erst);
    write64(interrupter, 0x18, (u64)(unsigned long)event_ring);
    write32(interrupter, 0x04, 4000); /* One millisecond moderation. */

    __asm__ volatile("mfence" ::: "memory");
    write32(operational, 0x00, 1u); /* Run; polling is used initially. */
    if (!wait32(operational, 0x04, 1u, 0)) return 0;
    for (u32 port = 0; port < max_ports; port++) {
        u32 portsc = read32(operational, 0x400 + port * 0x10);
        if (portsc & 1u) ports_connected++;
    }
    return 1;
}

int xhci_init(void) {
    controllers_ready = 0;
    ports_connected = 0;
    int after = -1;
    const PciDevice *device;
    while ((device = pci_find_class(0x0c, 0x03, after))) {
        after = (int)(device - pci_device_at(0));
        if (device->prog_if != 0x30) continue;
        if (initialize_controller(device)) controllers_ready++;
    }
    if (controllers_ready) {
        char count[12], ports[12];
        number(count, (u32)controllers_ready);
        number(ports, (u32)ports_connected);
        serial("MayOS: xHCI generic controller ready, controllers=");
        serial(count); serial(" connected-ports="); serial(ports); serial("\n");
    }
    return controllers_ready;
}

int xhci_controller_count(void) { return controllers_ready; }
int xhci_connected_port_count(void) { return ports_connected; }

#include "core.h"
#include "pci.h"
#include "mmio.h"
#include "task_manager.h"
#include "xhci.h"

enum {
    MAX_CONTROLLERS = 2, MAX_HID = 8, MAX_PORTS = 255, MAX_SCRATCH = 64,
    RING_SIZE = 256, CONTROL_BYTES = 1024, REPORT_BYTES = 64,
    TRB_NORMAL = 1, TRB_SETUP = 2, TRB_DATA = 3, TRB_STATUS = 4, TRB_LINK = 6,
    CMD_ENABLE = 9, CMD_DISABLE = 10, CMD_ADDRESS = 11, CMD_CONFIGURE = 12,
    CMD_EVALUATE = 13, EVENT_TRANSFER = 32, EVENT_COMMAND = 33,
    CC_SUCCESS = 1, CC_SHORT = 13,
};

typedef struct { u64 parameter; u32 status, control; } Trb;
typedef struct { u64 address; u32 size, reserved; } Erst;
typedef struct {
    Trb entries[RING_SIZE] __attribute__((aligned(4096)));
    u32 enqueue, cycle;
} Ring;

typedef struct {
    u32 output[32 * 16] __attribute__((aligned(4096)));
    u32 input[33 * 16] __attribute__((aligned(4096)));
    u8 control[CONTROL_BYTES] __attribute__((aligned(64)));
    u8 report[REPORT_BYTES] __attribute__((aligned(64)));
    Ring ep0, interrupt;
    u64 pending;
    u32 slot, port, speed, ep0_packet, dci, packet, protocol;
    u32 buttons, caps_lock, active;
    u8 previous[6], modifiers;
} HidDevice;

typedef struct {
    u64 dcbaa[256] __attribute__((aligned(64)));
    Ring command;
    Trb events[RING_SIZE] __attribute__((aligned(4096)));
    Erst erst __attribute__((aligned(64)));
    u64 scratch_array[MAX_SCRATCH] __attribute__((aligned(64)));
    u8 scratch[MAX_SCRATCH][4096] __attribute__((aligned(4096)));
    HidDevice devices[MAX_HID];
    volatile u8 *op, *intr, *doorbells;
    u32 event_read, event_cycle, ports, slots, stride;
    u32 next_scan;
    u8 usb3[MAX_PORTS], slot_type[MAX_PORTS], attempted[MAX_PORTS];
    int failed;
} Controller;

static Controller controllers[MAX_CONTROLLERS];
static int controllers_ready, ports_connected;
typedef struct { int dx, dy, buttons; } PointerEvent;
static PointerEvent pointer_events[32];
static u32 pointer_read, pointer_write;
static int pointer_buttons;
static int keys[32];
static u32 keys_read, keys_write;

static u64 physical(const void *pointer) { return (u64)(uptr)pointer; }
static void barrier(void) { __asm__ volatile("mfence" ::: "memory"); }

static u32 read32(volatile u8 *base, u32 offset) {
    return *(volatile u32 *)(base + offset);
}
static void write32(volatile u8 *base, u32 offset, u32 value) {
    *(volatile u32 *)(base + offset) = value;
}
static void write64(volatile u8 *base, u32 offset, u64 value) {
    *(volatile u64 *)(base + offset) = value;
}
static int wait32(volatile u8 *base, u32 offset, u32 mask, u32 value, u32 limit) {
    u32 start = ticks;
    do {
        if ((read32(base, offset) & mask) == value) return 1;
        __asm__ volatile("pause");
    } while (ticks - start < limit);
    return 0;
}

static void ring_init(Ring *ring) {
    memset(ring, 0, sizeof(*ring));
    ring->cycle = 1;
    ring->entries[RING_SIZE - 1].parameter = physical(ring->entries);
}
static u64 ring_push(Ring *ring, u64 parameter, u32 status, u32 control) {
    volatile Trb *trb = &ring->entries[ring->enqueue++];
    trb->parameter = parameter;
    trb->status = status;
    barrier();
    trb->control = control | ring->cycle;
    if (ring->enqueue == RING_SIZE - 1) {
        volatile Trb *link = &ring->entries[RING_SIZE - 1];
        link->control = (TRB_LINK << 10) | 2u | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1u;
    }
    return (u64)(uptr)trb;
}
static void ring_doorbell(Controller *hc, u32 slot, u32 endpoint) {
    barrier();
    write32(hc->doorbells, slot * 4, endpoint);
}

static int next_event(Controller *hc, Trb *value) {
    const volatile Trb *event = &hc->events[hc->event_read];
    u32 control = event->control;
    if ((control & 1) != hc->event_cycle) return 0;
    barrier();
    *value = (Trb){event->parameter, event->status, control};
    if (++hc->event_read == RING_SIZE) {
        hc->event_read = 0;
        hc->event_cycle ^= 1u;
    }
    write64(hc->intr, 0x18, physical(&hc->events[hc->event_read]) | 8u);
    return 1;
}

static void queue_key(int key) {
    if (key && keys_write - keys_read < sizeof(keys) / sizeof(keys[0]))
        keys[keys_write++ % (sizeof(keys) / sizeof(keys[0]))] = key;
}
static int current_buttons(void) {
    int buttons = 0;
    for (int i = 0; i < controllers_ready; i++) {
        if (controllers[i].failed) continue;
        for (u32 d = 0; d < MAX_HID; d++) buttons |= (int)controllers[i].devices[d].buttons;
    }
    return buttons;
}

static void queue_pointer(int dx, int dy) {
    u32 capacity = sizeof(pointer_events) / sizeof(pointer_events[0]);
    int buttons = current_buttons();
    if (pointer_write - pointer_read >= capacity) {
        PointerEvent *last = &pointer_events[(pointer_write - 1) % capacity];
        last->dx += dx;
        last->dy += dy;
        last->buttons = buttons;
    } else {
        pointer_events[pointer_write++ % capacity] = (PointerEvent){dx, dy, buttons};
    }
}
static int keyboard_key(HidDevice *dev, u8 usage, u8 mod) {
    int shift = (mod & 0x22) != 0;
    if (usage >= 4 && usage <= 29) {
        int key = ((shift ^ (int)dev->caps_lock) ? 'A' : 'a') + usage - 4;
        if (mod & 0x11) return usage == 22 ? 0x106 : 0; /* Ctrl+S */
        return key;
    }
    if (usage >= 30 && usage <= 39)
        return shift ? "!@#$%^&*()"[usage - 30] : "1234567890"[usage - 30];
    if (usage >= 40 && usage <= 56) {
        static const char plain[] = "\n\033\b\t -=[]\\\0;'\x60,./";
        static const char upper[] = "\n\033\b\t _+{}|\0:\"~<>?";
        return (shift ? upper : plain)[usage - 40];
    }
    switch (usage) {
        case 57: dev->caps_lock ^= 1u; return 0;
        case 58: return 0x105;
        case 59: return MAYOS_KEY_TASK_MANAGER;
        case 60: return MAYOS_KEY_IME;
        case 76: return 0x103;
        case 79: return MAYOS_KEY_RIGHT;
        case 80: return MAYOS_KEY_LEFT;
        case 81: return 0x102;
        case 82: return 0x101;
        case 88: return '\n';
        case 89: case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97: return '1' + usage - 89;
        case 98: return '0';
        case 99: return '.';
        default: return 0;
    }
}
static void boot_report(HidDevice *dev, u32 length) {
    if (dev->protocol == 1 && length >= 8) {
        for (u32 i = 2; i < 8; i++)
            if (dev->report[i] > 0 && dev->report[i] < 4) return; /* Rollover. */
        u8 modifiers = dev->report[0];
        /* Boot reports encode left/right GUI in the modifier byte, not a usage slot. */
        if ((modifiers & 0x88u) && !(dev->modifiers & 0x88u))
            queue_key(MAYOS_KEY_LAUNCHER);
        dev->modifiers = modifiers;
        for (u32 i = 2; i < 8; i++) {
            u8 usage = dev->report[i];
            int already = 0;
            for (u32 j = 0; j < 6; j++) already |= dev->previous[j] == usage;
            for (u32 j = 2; j < i; j++) already |= dev->report[j] == usage;
            if (usage && !already) queue_key(keyboard_key(dev, usage, dev->report[0]));
        }
        memcpy(dev->previous, dev->report + 2, sizeof(dev->previous));
    } else if (dev->protocol == 2 && length >= 3) {
        int changed = dev->buttons != (dev->report[0] & 7u);
        dev->buttons = dev->report[0] & 7u;
        if (dev->report[1] || dev->report[2] || changed)
            queue_pointer((signed char)dev->report[1], (signed char)dev->report[2]);
    }
}
static void submit_report(Controller *hc, HidDevice *dev) {
    dev->pending = ring_push(&dev->interrupt, physical(dev->report), dev->packet,
                            (TRB_NORMAL << 10) | (1u << 5) | (1u << 2));
    ring_doorbell(hc, dev->slot, dev->dci);
}
static void dispatch_event(Controller *hc, const Trb *event) {
    if (((event->control >> 10) & 63u) != EVENT_TRANSFER) return;
    u32 slot = event->control >> 24, endpoint = (event->control >> 16) & 31u;
    for (u32 i = 0; i < MAX_HID; i++) {
        HidDevice *dev = &hc->devices[i];
        if (dev->slot != slot || dev->dci != endpoint || !dev->pending ||
            event->parameter != dev->pending) continue;
        dev->pending = 0;
        u32 code = event->status >> 24, residual = event->status & 0xffffffu;
        if ((code == CC_SUCCESS || code == CC_SHORT) && residual <= dev->packet) {
            boot_report(dev, dev->packet - residual);
            submit_report(hc, dev);
        } else if (dev->buttons) {
            dev->buttons = 0;
            queue_pointer(0, 0);
        }
        return;
    }
}
static int wait_event(Controller *hc, u32 kind, u64 target, u32 slot, u32 *event_slot) {
    u32 start = ticks;
    do {
        Trb event;
        if (!next_event(hc, &event)) {
            __asm__ volatile("pause");
            continue;
        }
        u32 type = (event.control >> 10) & 63u, id = event.control >> 24;
        if (type == kind && (!slot || slot == id)) {
            u32 code = event.status >> 24;
            if (event.parameter == target) {
                if (event_slot) *event_slot = id;
                return code == CC_SUCCESS;
            }
            /* A failed Setup/Data stage can stop before the Status TRB. */
            if (kind == EVENT_TRANSFER && ((event.control >> 16) & 31u) == 1 &&
                code != CC_SUCCESS && code != CC_SHORT) return 0;
        }
        dispatch_event(hc, &event);
    } while (ticks - start < 50);
    if (kind == EVENT_COMMAND) hc->failed = 1; /* Never reuse a timed-out command ring. */
    return 0;
}
static int command(Controller *hc, u32 type, u64 data, u32 flags, u32 *slot) {
    if (hc->failed) return 0;
    u64 target = ring_push(&hc->command, data, 0, (type << 10) | flags);
    ring_doorbell(hc, 0, 0);
    return wait_event(hc, EVENT_COMMAND, target, 0, slot);
}

static int control(Controller *hc, HidDevice *dev, u32 bm, u32 request,
                   u32 value, u32 index, u32 length) {
    if (length > CONTROL_BYTES || (length && !(bm & 0x80))) return 0;
    if (length) memset(dev->control, 0, length);
    u64 setup = bm | (request << 8) | (value << 16) |
                ((u64)index << 32) | ((u64)length << 48);
    ring_push(&dev->ep0, setup, 8, (TRB_SETUP << 10) | (1u << 6) | (length ? 3u << 16 : 0));
    if (length)
        ring_push(&dev->ep0, physical(dev->control), length, (TRB_DATA << 10) | (1u << 16));
    u64 status = ring_push(&dev->ep0, 0, 0, (TRB_STATUS << 10) | (1u << 5) |
                           (!length ? 1u << 16 : 0));
    ring_doorbell(hc, dev->slot, 1);
    return wait_event(hc, EVENT_TRANSFER, status, dev->slot, 0);
}

static void endpoint(u32 *context, Ring *ring, u32 interval, u32 type, u32 packet) {
    u64 dequeue = physical(ring->entries) | 1u;
    context[0] = interval << 16;
    context[1] = (3u << 1) | (type << 3) | (packet << 16);
    context[2] = (u32)dequeue;
    context[3] = (u32)(dequeue >> 32);
    context[4] = packet | (type == 7 ? packet << 16 : 0);
}

static void drop_device(Controller *hc, HidDevice *dev) {
    if (!dev->slot) return;
    int attached = dev->active != 0;
    dev->active = 0;
    dev->pending = 0;
    if (dev->buttons) {
        dev->buttons = 0;
        queue_pointer(0, 0);
    }
    if (attached)
        serial(dev->protocol == 1 ? "MayOS: USB boot keyboard disconnected\n"
                                  : "MayOS: USB boot mouse disconnected\n");
    u32 slot = dev->slot;
    if (command(hc, CMD_DISABLE, 0, slot << 24, 0)) {
        hc->dcbaa[slot] = 0;
        dev->slot = 0;
    }
}

static int parse_hid(const u8 *data, u32 length, u32 *interface, u32 *protocol,
                     u32 *dci, u32 *packet, u32 *interval) {
    u32 kind = 0;
    for (u32 at = 0; at + 2 <= length;) {
        u32 size = data[at], type = data[at + 1];
        if (size < 2 || size > length - at) return 0;
        if (type == 4 && size >= 9) {
            kind = data[at + 3] == 0 && data[at + 5] == 3 && data[at + 6] == 1
                ? data[at + 7] : 0;
            *interface = data[at + 2];
        } else if (type == 5 && size >= 7 && (kind == 1 || kind == 2) &&
                   (data[at + 2] & 0x80) && (data[at + 2] & 15u) &&
                   (data[at + 3] & 3u) == 3) {
            u32 mps = data[at + 4] | ((u32)data[at + 5] << 8);
            if (mps < (kind == 1 ? 8u : 3u) || mps > REPORT_BYTES) return 0;
            *protocol = kind;
            *dci = (data[at + 2] & 15u) * 2 + 1;
            *packet = mps;
            *interval = data[at + 6];
            return 1;
        }
        at += size;
    }
    return 0;
}

static int enumerate_hid(Controller *hc, HidDevice *dev) {
    u32 *slot = dev->input + hc->stride;
    u32 *ep0 = slot + hc->stride;
    u32 port = dev->port, speed = dev->speed;
    dev->ep0_packet = speed >= 4 ? 512 : speed == 3 ? 64 : 8;
    hc->dcbaa[dev->slot] = physical(dev->output);
    dev->input[1] = 3;
    slot[0] = (speed << 20) | (1u << 27);
    slot[1] = port << 16;
    endpoint(ep0, &dev->ep0, 0, 4, dev->ep0_packet);
    if (!command(hc, CMD_ADDRESS, physical(dev->input), dev->slot << 24, 0)) return 0;
    if (!control(hc, dev, 0x80, 6, 0x0100, 0, 8) ||
        dev->control[0] < 8 || dev->control[1] != 1) return 0;
    if (speed == 1) {
        u32 mps = dev->control[7];
        if (mps != 8 && mps != 16 && mps != 32 && mps != 64) return 0;
        if (mps != dev->ep0_packet) {
            dev->input[1] = 2;
            ep0[1] = (ep0[1] & 0xffffu) | (mps << 16);
            if (!command(hc, CMD_EVALUATE, physical(dev->input), dev->slot << 24, 0)) return 0;
        }
    }
    if (!control(hc, dev, 0x80, 6, 0x0200, 0, 9) ||
        dev->control[0] != 9 || dev->control[1] != 2) return 0;
    u32 total = dev->control[2] | ((u32)dev->control[3] << 8);
    if (total < 9 || total > CONTROL_BYTES || !control(hc, dev, 0x80, 6, 0x0200, 0, total))
        return 0;
    u32 config = dev->control[5], iface = 0, int_interval = 0;
    if (!config || !parse_hid(dev->control, total, &iface, &dev->protocol,
                              &dev->dci, &dev->packet, &int_interval)) return 0;
    if (!control(hc, dev, 0, 9, config, 0, 0) ||
        !control(hc, dev, 0x21, 0x0b, 0, iface, 0)) return 0;
    u32 interval = 0;
    if (speed < 3) {
        interval = 3;
        for (u32 milliseconds = int_interval; milliseconds > 1; milliseconds >>= 1) interval++;
    } else {
        interval = int_interval > 16 ? 15 : int_interval ? int_interval - 1 : 0;
    }
    dev->input[1] = 1u | (1u << dev->dci);
    slot[0] = (speed << 20) | (dev->dci << 27);
    endpoint(slot + hc->stride * dev->dci, &dev->interrupt, interval, 7, dev->packet);
    if (!command(hc, CMD_CONFIGURE, physical(dev->input), dev->slot << 24, 0)) return 0;
    dev->active = 1;
    submit_report(hc, dev);
    serial(dev->protocol == 1 ? "MayOS: USB boot keyboard ready\n" : "MayOS: USB boot mouse ready\n");
    return 1;
}

static void try_port(Controller *hc, u32 port, u32 state) {
    HidDevice *dev = 0;
    for (u32 i = 0; i < MAX_HID; i++)
        if (!hc->devices[i].slot) { dev = &hc->devices[i]; break; }
    if (!dev) return;
    u32 offset = 0x400 + port * 16;
    if (!(state & 2u)) {
        u32 reset = hc->usb3[port] ? 1u << 31 : 1u << 4;
        /* Preserve PP, indicator and wake controls. Do not echo W1C bits. */
        write32(hc->op, offset, (state & ((1u << 9) | (3u << 14) | (7u << 25))) | reset);
        if (!wait32(hc->op, offset, 3u | reset, 3u, 25)) return;
        /* USB2 reset recovery is at least 10 ms before the first request. */
        if (!hc->usb3[port]) {
            u32 start = ticks;
            while (ticks - start < 2) __asm__ volatile("pause");
        }
        state = read32(hc->op, offset);
    }
    u32 speed = (state >> 10) & 15u, id = 0;
    if (speed < 1 || speed > 4 ||
        !command(hc, CMD_ENABLE, 0, (u32)hc->slot_type[port] << 16, &id) ||
        !id || id > hc->slots) return;
    memset(dev, 0, sizeof(*dev));
    dev->slot = id;
    dev->port = port + 1;
    dev->speed = speed;
    ring_init(&dev->ep0);
    ring_init(&dev->interrupt);
    if (!enumerate_hid(hc, dev)) drop_device(hc, dev);
}

static int firmware_handoff(Controller *hc, volatile u8 *cap, u32 hcc) {
    for (u32 off = (hcc >> 16) * 4u, guard = 0; off && off <= 0x10000 && guard < 256; guard++) {
        u32 value = read32(cap, off), id = value & 255u, next = (value >> 8) & 255u;
        if (id == 1) {
            write32(cap, off, value | (1u << 24));
            if (!wait32(cap, off, 1u << 16, 0, 100)) return 0;
            /* Disable the legacy USB SMI sources once BIOS ownership is gone. */
            write32(cap, off + 4, 7u << 29);
        } else if (id == 2) {
            u32 ports = read32(cap, off + 8);
            u32 first = ports & 255u, count = (ports >> 8) & 255u;
            u32 slot_type = read32(cap, off + 12) & 31u;
            if (first && first <= hc->ports && count <= hc->ports - first + 1)
                for (u32 i = first - 1; i < first - 1 + count; i++) {
                    hc->usb3[i] = value >> 24 >= 3;
                    hc->slot_type[i] = (u8)slot_type;
                }
        }
        if (!next) break;
        off += next * 4;
    }
    return 1;
}

static int initialize_controller(Controller *hc, const PciDevice *device) {
    u32 bar = device->bar[0];
    if (bar & 1u) return 0;
    u64 address = bar & ~0x0fu;
    if ((bar & 6u) == 4u) address |= (u64)device->bar[1] << 32;
    if (!address) return 0;
    volatile u8 *cap = mmio_map(address, 0x20000);
    if (!cap) {
        serial("MayOS: xHCI MMIO mapping unavailable\n");
        return 0;
    }
    pci_enable_memory_busmaster(device);
    if (address >= (64ULL << 30)) serial("MayOS: xHCI uses a 64-bit high MMIO BAR\n");
    u8 cap_length = *cap;
    if (cap_length < 0x20) return 0;
    u32 params1 = read32(cap, 4), params2 = read32(cap, 8), hcc = read32(cap, 0x10);
    u32 db = read32(cap, 0x14) & ~3u, rt = read32(cap, 0x18) & ~31u;
    u32 scratch_count = (((params2 >> 21) & 31u) << 5) | ((params2 >> 27) & 31u);
    if (!db || !rt || db > 0x10000 || rt > 0x10000 || scratch_count > MAX_SCRATCH) return 0;
    memset(hc, 0, sizeof(*hc));
    hc->slots = params1 & 255u;
    hc->ports = params1 >> 24;
    hc->stride = hcc & 4u ? 16 : 8;
    if (!hc->slots || !hc->ports || !firmware_handoff(hc, cap, hcc)) return 0;
    hc->op = cap + cap_length;
    hc->intr = cap + rt + 0x20;
    hc->doorbells = cap + db;
    if (!(read32(hc->op, 8) & 1u)) return 0;
    write32(hc->op, 0, 0);
    if (!wait32(hc->op, 4, 1u, 1u, 10)) return 0;
    write32(hc->op, 0, 2u);
    if (!wait32(hc->op, 0, 2u, 0, 100) || !wait32(hc->op, 4, 1u << 11, 0, 100)) return 0;
    ring_init(&hc->command);
    hc->event_cycle = 1;
    if (scratch_count) {
        for (u32 i = 0; i < scratch_count; i++) hc->scratch_array[i] = physical(hc->scratch[i]);
        hc->dcbaa[0] = physical(hc->scratch_array);
    }
    write64(hc->op, 0x30, physical(hc->dcbaa));
    write64(hc->op, 0x18, physical(hc->command.entries) | 1u);
    write32(hc->op, 0x38, hc->slots);
    hc->erst = (Erst){physical(hc->events), RING_SIZE, 0};
    write32(hc->intr, 0, 0);
    write32(hc->intr, 8, 1);
    write64(hc->intr, 0x10, physical(&hc->erst));
    write64(hc->intr, 0x18, physical(hc->events));
    barrier();
    write32(hc->op, 0, 1u); /* Polling with host interrupts disabled. */
    if (!wait32(hc->op, 4, 1u, 0, 10)) {
        write32(hc->op, 0, 0);
        return 0;
    }
    for (u32 port = 0; port < hc->ports; port++) {
        u32 offset = 0x400 + port * 16, state = read32(hc->op, offset);
        if (state & 1u) ports_connected++;
        if ((hcc & 8u) && !(state & (1u << 9)))
            write32(hc->op, offset, (state & ((3u << 14) | (7u << 25))) | (1u << 9));
    }
    return 1;
}

int xhci_init(void) {
    int after = -1;
    const PciDevice *device;
    while (controllers_ready < MAX_CONTROLLERS && (device = pci_find_class(0x0c, 0x03, after))) {
        after = (int)(device - pci_device_at(0));
        if (device->prog_if == 0x30 && initialize_controller(&controllers[controllers_ready], device))
            controllers_ready++;
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
int xhci_boot_input_count(int protocol) {
    int count = 0;
    for (int i = 0; i < controllers_ready; i++) {
        if (controllers[i].failed) continue;
        for (u32 d = 0; d < MAX_HID; d++)
            if (controllers[i].devices[d].pending && controllers[i].devices[d].protocol == (u32)protocol)
                count++;
    }
    return count;
}

void xhci_poll(XhciInput *input) {
    for (int i = 0; i < controllers_ready; i++) {
        Controller *hc = &controllers[i];
        if (hc->failed) continue;
        Trb event;
        for (u32 n = 0; n < RING_SIZE && next_event(hc, &event); n++)
            dispatch_event(hc, &event);
        if (ticks - hc->next_scan < 20) continue;
        hc->next_scan = ticks;
        int probed = 0;
        for (u32 port = 0; port < hc->ports; port++) {
            u32 offset = 0x400 + port * 16;
            u32 state = read32(hc->op, offset), changed = state & (1u << 17);
            if (changed)
                write32(hc->op, offset, (state & ((1u << 9) | (3u << 14) | (7u << 25))) | changed);
            if (hc->attempted[port] && (!(state & 1u) || changed)) {
                hc->attempted[port] = 0;
                for (u32 d = 0; d < MAX_HID; d++)
                    if (hc->devices[d].slot && hc->devices[d].port == port + 1)
                        drop_device(hc, &hc->devices[d]);
            }
            if ((state & 1u) && !probed && !hc->attempted[port]) {
                hc->attempted[port] = 1;
                try_port(hc, port, state);
                probed = 1;
            }
        }
    }
    *input = (XhciInput){.buttons=pointer_buttons};
    if (pointer_read != pointer_write) {
        PointerEvent event = pointer_events[pointer_read++ %
            (sizeof(pointer_events) / sizeof(pointer_events[0]))];
        input->dx = event.dx;
        input->dy = event.dy;
        input->buttons = event.buttons;
        input->changed = 1;
    } else {
        input->buttons = current_buttons();
        input->changed = input->buttons != pointer_buttons;
    }
    pointer_buttons = input->buttons;
    if (keys_read != keys_write) input->key = keys[keys_read++ % (sizeof(keys) / sizeof(keys[0]))];
}

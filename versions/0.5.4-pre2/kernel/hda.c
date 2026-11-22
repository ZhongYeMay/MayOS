#include "core.h"
#include "pci.h"
#include "hda.h"

#define HDA_TIMEOUT 10000000u
#define HDA_RING_MAX 256

static u32 corb[HDA_RING_MAX] __attribute__((aligned(128)));
static u64 rirb[HDA_RING_MAX] __attribute__((aligned(128)));
static volatile u8 *registers;
static u16 ring_entries;
static u16 corb_write, rirb_read;
static int codecs_ready, outputs_ready;

static u8 r8(u32 offset) { return *(volatile u8 *)(registers + offset); }
static u16 r16(u32 offset) { return *(volatile u16 *)(registers + offset); }
static u32 r32(u32 offset) { return *(volatile u32 *)(registers + offset); }
static void w8(u32 offset, u8 value) { *(volatile u8 *)(registers + offset) = value; }
static void w16(u32 offset, u16 value) { *(volatile u16 *)(registers + offset) = value; }
static void w32(u32 offset, u32 value) { *(volatile u32 *)(registers + offset) = value; }

static int wait_bit(u32 offset, u32 mask, int set) {
    for (u32 timeout = 0; timeout < HDA_TIMEOUT; timeout++)
        if (((r32(offset) & mask) != 0) == set) return 1;
    return 0;
}

static int select_ring_size(u32 register_offset) {
    u8 supported = r8(register_offset);
    if (supported & (1u << 6)) {
        w8(register_offset, (supported & ~3u) | 2u);
        return 256;
    }
    if (supported & (1u << 5)) {
        w8(register_offset, (supported & ~3u) | 1u);
        return 16;
    }
    if (supported & (1u << 4)) {
        w8(register_offset, supported & ~3u);
        return 2;
    }
    return 0;
}

static int setup_rings(void) {
    w8(0x4c, 0); w8(0x5c, 0);
    int corb_entries = select_ring_size(0x4e);
    int rirb_entries = select_ring_size(0x5e);
    ring_entries = (u16)(corb_entries < rirb_entries ? corb_entries : rirb_entries);
    if (ring_entries < 2) return 0;
    memset(corb, 0, sizeof(corb)); memset(rirb, 0, sizeof(rirb));
    u64 corb_address = (u64)(unsigned long)corb;
    u64 rirb_address = (u64)(unsigned long)rirb;
    w32(0x40, (u32)corb_address); w32(0x44, (u32)(corb_address >> 32));
    w32(0x50, (u32)rirb_address); w32(0x54, (u32)(rirb_address >> 32));
    w16(0x4a, 1u << 15); w16(0x4a, 0);
    w16(0x48, 0); corb_write = 0;
    w16(0x58, 1u << 15); rirb_read = r16(0x58) & 0xffu;
    w16(0x5a, 1); /* Interrupt after each response; polled status is used. */
    w8(0x5d, 0x05); /* Clear overrun/response status. */
    w8(0x5c, 1u << 1); w8(0x4c, 1u << 1);
    return (r8(0x4c) & 2u) && (r8(0x5c) & 2u);
}

static int immediate_verb(u32 command, u32 *response) {
    for (u32 timeout = 0; timeout < HDA_TIMEOUT; timeout++)
        if (!(r16(0x68) & 1u)) break;
        else if (timeout + 1 == HDA_TIMEOUT) return 0;
    w16(0x68, 1u << 1); /* Clear stale Immediate Result Valid. */
    w32(0x60, command);
    w16(0x68, 1u); /* Immediate Command Busy: launch. */
    for (u32 timeout = 0; timeout < HDA_TIMEOUT; timeout++) {
        u16 status = r16(0x68);
        if (status & (1u << 1)) {
            *response = r32(0x64);
            w16(0x68, 1u << 1);
            return 1;
        }
    }
    return 0;
}

int hda_verb(u8 codec, u8 node, u16 verb, u8 payload, u32 *response) {
    if (!registers || codec > 14 || node > 127 || !response) return 0;
    u16 next = (u16)((corb_write + 1u) % ring_entries);
    u32 encoded = ((u32)codec << 28) | ((u32)node << 20) |
                  ((u32)(verb & 0x0fffu) << 8) | payload;
    corb[next] = encoded;
    __asm__ volatile("mfence" ::: "memory");
    corb_write = next; w16(0x48, corb_write);
    for (u32 timeout = 0; timeout < HDA_TIMEOUT / 100u; timeout++) {
        u16 hardware = r16(0x58) & 0xffu;
        if (hardware == rirb_read) continue;
        rirb_read = (u16)((rirb_read + 1u) % ring_entries);
        u64 entry = rirb[rirb_read];
        if (((entry >> 32) & 0x0fu) != codec) continue;
        *response = (u32)entry;
        return 1;
    }
    return immediate_verb(encoded, response);
}

static int parameter(u8 codec, u8 node, u8 id, u32 *response) {
    return hda_verb(codec, node, 0x0f00, id, response);
}

static int enumerate_codec(u8 codec) {
    u32 vendor, nodes;
    if (!parameter(codec, 0, 0x00, &vendor) || vendor == 0xffffffffu ||
        !parameter(codec, 0, 0x04, &nodes)) return 0;
    codecs_ready++;
    u8 first = (u8)(nodes >> 16), count = (u8)nodes;
    for (u32 group = first; group < (u32)first + count; group++) {
        u32 type, widgets;
        if (!parameter(codec, (u8)group, 0x05, &type) || (type & 0xffu) != 1u ||
            !parameter(codec, (u8)group, 0x04, &widgets)) continue;
        u8 widget_first = (u8)(widgets >> 16), widget_count = (u8)widgets;
        for (u32 node = widget_first; node < (u32)widget_first + widget_count; node++) {
            u32 caps;
            if (!parameter(codec, (u8)node, 0x09, &caps)) continue;
            u8 widget_type = (u8)((caps >> 20) & 0x0fu);
            if (widget_type == 0) outputs_ready++;
            else if (widget_type == 4) {
                u32 pin_caps;
                if (parameter(codec, (u8)node, 0x0c, &pin_caps) &&
                    (pin_caps & (1u << 4))) outputs_ready++;
            }
        }
    }
    return 1;
}

int hda_init(void) {
    registers = 0; codecs_ready = 0; outputs_ready = 0;
    const PciDevice *device = pci_find_class(0x04, 0x03, -1);
    if (!device) return 0;
    u32 bar = device->bar[0];
    if ((bar & 1u) || !(bar & ~0x0fu)) return 0;
    u64 address = bar & ~0x0fu;
    if ((bar & 6u) == 4u) address |= (u64)device->bar[1] << 32;
    if (address >= (64ULL << 30)) return 0;
    pci_enable_memory_busmaster(device);
    registers = (volatile u8 *)(unsigned long)address;
    w32(0x20, 0); /* Poll until the interrupt layer gains MSI support. */
    w32(0x08, r32(0x08) & ~1u);
    if (!wait_bit(0x08, 1u, 0)) { registers = 0; return 0; }
    w32(0x08, r32(0x08) | 1u);
    if (!wait_bit(0x08, 1u, 1) || !setup_rings()) {
        registers = 0; return 0;
    }
    u16 codecs = 0;
    for (u32 timeout = 0; timeout < HDA_TIMEOUT; timeout++) {
        codecs = r16(0x0e) & 0x7fffu;
        if (codecs) break;
    }
    for (u8 codec = 0; codec < 15; codec++)
        if (codecs & (1u << codec)) enumerate_codec(codec);
    /* Some controllers do not latch STATESTS when firmware leaves WAKEEN
       disabled.  Codec address 0 is the mandated first address, so a real
       GetParameter response is a safe presence probe and avoids vendor IDs. */
    if (!codecs_ready) enumerate_codec(0);
    char codec_count[12], output_count[12];
    number(codec_count, (u32)codecs_ready); number(output_count, (u32)outputs_ready);
    serial("MayOS: HDA generic CORB/RIRB ready, codecs="); serial(codec_count);
    serial(" output-widgets="); serial(output_count); serial("\n");
    return 1;
}

int hda_codec_count(void) { return codecs_ready; }
int hda_output_widget_count(void) { return outputs_ready; }

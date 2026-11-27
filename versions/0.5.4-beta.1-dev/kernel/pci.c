#include "pci.h"
#include "io.h"
#include "acpi.h"

static PciDevice devices[PCI_MAX_DEVICES];
static int device_count;

static u32 pci_address(u8 bus, u8 slot, u8 function, u8 offset) {
    return 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
           ((u32)function << 8) | (offset & 0xfcu);
}

u32 pci_config_read32(u8 bus, u8 slot, u8 function, u8 offset) {
    volatile u32 *ecam = acpi_pci_config_address(bus, slot, function, offset);
    if (ecam) return *ecam;
    outl(0xcf8, pci_address(bus, slot, function, offset));
    return inl(0xcfc);
}

u16 pci_config_read16(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_config_read32(bus, slot, function, offset);
    return (u16)(value >> ((offset & 2u) * 8));
}

u8 pci_config_read8(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_config_read32(bus, slot, function, offset);
    return (u8)(value >> ((offset & 3u) * 8));
}

void pci_config_write32(u8 bus, u8 slot, u8 function, u8 offset, u32 value) {
    volatile u32 *ecam = acpi_pci_config_address(bus, slot, function, offset);
    if (ecam) { *ecam = value; return; }
    outl(0xcf8, pci_address(bus, slot, function, offset));
    outl(0xcfc, value);
}

void pci_config_write16(u8 bus, u8 slot, u8 function, u8 offset, u16 value) {
    volatile u32 *ecam = acpi_pci_config_address(bus, slot, function, offset);
    if (ecam) {
        *((volatile u16 *)ecam + ((offset & 2u) >> 1)) = value;
        return;
    }
    outl(0xcf8, pci_address(bus, slot, function, offset));
    outw((u16)(0xcfc + (offset & 2u)), value);
}

void pci_enable_memory_busmaster(const PciDevice *device) {
    if (!device) return;
    u16 command = pci_config_read16(device->bus, device->slot, device->function, 0x04);
    command |= 0x0006; /* memory-space decode and bus mastering */
    pci_config_write16(device->bus, device->slot, device->function, 0x04, command);
}

static void serial_hex(u32 value, int digits) {
    static const char hex[] = "0123456789abcdef";
    char out[9];
    for (int i = 0; i < digits; i++)
        out[i] = hex[(value >> ((digits - i - 1) * 4)) & 15];
    out[digits] = 0;
    serial(out);
}

static const char *class_name(const PciDevice *device) {
    if (device->class_code == 0x01 && device->subclass == 0x01) return "IDE";
    if (device->class_code == 0x01 && device->subclass == 0x06) return "AHCI";
    if (device->class_code == 0x02) return "network";
    if (device->class_code == 0x03) return "display";
    if (device->class_code == 0x04 && device->subclass == 0x03) return "audio";
    if (device->class_code == 0x06 && device->subclass == 0x00) return "host bridge";
    if (device->class_code == 0x06 && device->subclass == 0x01) return "ISA bridge";
    if (device->class_code == 0x06 && device->subclass == 0x04) return "PCI bridge";
    if (device->class_code == 0x0c && device->subclass == 0x03 &&
        device->prog_if == 0x30) return "xHCI";
    if (device->class_code == 0x0c && device->subclass == 0x03) return "USB";
    return "unclaimed";
}

static void append_text(char **at, char *end, const char *source) {
    while (*source && *at + 1 < end) *(*at)++ = *source++;
    **at = 0;
}

static void append_hex(char **at, char *end, u32 value, int digits) {
    static const char hex[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0 && *at + 1 < end; i--)
        *(*at)++ = hex[(value >> (i * 4)) & 15];
    **at = 0;
}

void pci_describe(const PciDevice *device, char *output, int capacity) {
    if (!output || capacity <= 0) return;
    char *at = output, *end = output + capacity;
    *at = 0;
    if (!device) { append_text(&at, end, "invalid PCI device"); return; }
    append_hex(&at, end, device->bus, 2); append_text(&at, end, ":");
    append_hex(&at, end, device->slot, 2); append_text(&at, end, ".");
    append_hex(&at, end, device->function, 1); append_text(&at, end, " ");
    append_hex(&at, end, device->vendor_id, 4); append_text(&at, end, ":");
    append_hex(&at, end, device->device_id, 4); append_text(&at, end, " ");
    append_text(&at, end, class_name(device)); append_text(&at, end, " class ");
    append_hex(&at, end, device->class_code, 2); append_text(&at, end, ":");
    append_hex(&at, end, device->subclass, 2); append_text(&at, end, ":");
    append_hex(&at, end, device->prog_if, 2);
}

static u32 read_capabilities(const PciDevice *device) {
    if (!(pci_config_read16(device->bus, device->slot, device->function, 0x06) & 0x10))
        return 0;
    u8 pointer = pci_config_read8(device->bus, device->slot, device->function, 0x34) & 0xfcu;
    u32 found = 0;
    for (int guard = 0; pointer >= 0x40 && guard < 48; guard++) {
        u8 id = pci_config_read8(device->bus, device->slot, device->function, pointer);
        if (id == 0x01) found |= PCI_CAP_PM;
        else if (id == 0x05) found |= PCI_CAP_MSI;
        else if (id == 0x10) found |= PCI_CAP_PCIE;
        else if (id == 0x11) found |= PCI_CAP_MSIX;
        u8 next = pci_config_read8(device->bus, device->slot, device->function,
                                   (u8)(pointer + 1)) & 0xfcu;
        if (!next || next == pointer) break;
        pointer = next;
    }
    return found;
}

static void enumerate_function(u8 bus, u8 slot, u8 function) {
    u32 identity = pci_config_read32(bus, slot, function, 0x00);
    u16 vendor = (u16)identity;
    if (vendor == 0xffff || vendor == 0x0000) return;
    if (device_count >= PCI_MAX_DEVICES) return;
    PciDevice *device = &devices[device_count++];
    memset(device, 0, sizeof(*device));
    device->bus = bus; device->slot = slot; device->function = function;
    device->vendor_id = vendor; device->device_id = (u16)(identity >> 16);
    u32 class_data = pci_config_read32(bus, slot, function, 0x08);
    device->revision = (u8)class_data;
    device->prog_if = (u8)(class_data >> 8);
    device->subclass = (u8)(class_data >> 16);
    device->class_code = (u8)(class_data >> 24);
    device->header_type = pci_config_read8(bus, slot, function, 0x0e);
    int bars = (device->header_type & 0x7f) == 0 ? 6 : 2;
    for (int i = 0; i < bars; i++)
        device->bar[i] = pci_config_read32(bus, slot, function, (u8)(0x10 + i * 4));
    device->irq_line = pci_config_read8(bus, slot, function, 0x3c);
    device->irq_pin = pci_config_read8(bus, slot, function, 0x3d);
    device->capabilities = read_capabilities(device);
    serial("MayOS: PCI "); serial_hex(bus, 2); serial(":"); serial_hex(slot, 2);
    serial("."); serial_hex(function, 1); serial(" "); serial_hex(device->vendor_id, 4);
    serial(":"); serial_hex(device->device_id, 4); serial(" class ");
    serial_hex(device->class_code, 2); serial(":"); serial_hex(device->subclass, 2);
    serial(":"); serial_hex(device->prog_if, 2); serial(" ");
    serial(class_name(device)); serial("\n");
}

void pci_init(void) {
    device_count = 0;
    /* Prefer ACPI MCFG ECAM for PCIe. Configuration mechanism #1 remains the
       standards-defined fallback for conventional PCI and older firmware. */
    for (u32 bus = 0; bus < 256; bus++)
        for (u32 slot = 0; slot < 32; slot++) {
            u16 vendor = pci_config_read16((u8)bus, (u8)slot, 0, 0);
            if (vendor == 0xffff || vendor == 0) continue;
            enumerate_function((u8)bus, (u8)slot, 0);
            if (pci_config_read8((u8)bus, (u8)slot, 0, 0x0e) & 0x80)
                for (u32 function = 1; function < 8; function++)
                    enumerate_function((u8)bus, (u8)slot, (u8)function);
        }
    char count[12]; number(count, (u32)device_count);
    serial("MayOS: PCI enumeration complete, devices="); serial(count); serial("\n");
    if (device_count == PCI_MAX_DEVICES)
        serial("MayOS: PCI device table full; additional functions ignored\n");
}

int pci_device_count(void) { return device_count; }

const PciDevice *pci_device_at(int index) {
    return index >= 0 && index < device_count ? &devices[index] : 0;
}

const PciDevice *pci_find_class(u8 class_code, u8 subclass, int after) {
    for (int i = after + 1; i < device_count; i++)
        if (devices[i].class_code == class_code && devices[i].subclass == subclass)
            return &devices[i];
    return 0;
}

int pci_ide_primary_ports(u16 *command, u16 *control) {
    if (!command || !control) return 0;
    *command = 0x1f0; *control = 0x3f6;
    const PciDevice *ide = pci_find_class(0x01, 0x01, -1);
    if (!ide) return 0;
    /* prog-if bit 0 means native primary-channel mode.  In compatibility
       mode the ISA ports are mandated regardless of BAR contents. */
    if (!(ide->prog_if & 0x01)) return 1;
    if ((ide->bar[0] & 1) && (ide->bar[1] & 1)) {
        u32 io = ide->bar[0] & ~3u, ctrl = ide->bar[1] & ~3u;
        if (io <= 0xfff8 && ctrl <= 0xfffcu && io >= 0x100 && ctrl >= 0x100) {
            *command = (u16)io;
            *control = (u16)(ctrl + 2);
            return 1;
        }
    }
    serial("MayOS: PCI IDE native BARs invalid; ATA disabled for safety\n");
    *command = 0; *control = 0;
    return 0;
}

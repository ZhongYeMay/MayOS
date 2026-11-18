#ifndef MAYOS_PCI_H
#define MAYOS_PCI_H
#include "core.h"

#define PCI_MAX_DEVICES 96

typedef struct {
    u8 bus, slot, function;
    u16 vendor_id, device_id;
    u8 class_code, subclass, prog_if, revision, header_type;
    u32 bar[6];
    u8 irq_line, irq_pin;
    u32 capabilities;
} PciDevice;

enum {
    PCI_CAP_PM   = 1u << 0,
    PCI_CAP_MSI  = 1u << 1,
    PCI_CAP_PCIE = 1u << 2,
    PCI_CAP_MSIX = 1u << 3,
};

void pci_init(void);
int pci_device_count(void);
const PciDevice *pci_device_at(int index);
const PciDevice *pci_find_class(u8 class_code, u8 subclass, int after);
u32 pci_config_read32(u8 bus, u8 slot, u8 function, u8 offset);
u16 pci_config_read16(u8 bus, u8 slot, u8 function, u8 offset);
u8 pci_config_read8(u8 bus, u8 slot, u8 function, u8 offset);
void pci_config_write32(u8 bus, u8 slot, u8 function, u8 offset, u32 value);
void pci_config_write16(u8 bus, u8 slot, u8 function, u8 offset, u16 value);
void pci_enable_memory_busmaster(const PciDevice *device);
int pci_ide_primary_ports(u16 *command, u16 *control);
void pci_describe(const PciDevice *device, char *output, int capacity);

#endif

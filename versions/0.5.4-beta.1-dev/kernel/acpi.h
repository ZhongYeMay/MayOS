#ifndef MAYOS_ACPI_H
#define MAYOS_ACPI_H
#include "core.h"

void acpi_init(void);
volatile u32 *acpi_pci_config_address(u8 bus, u8 slot, u8 function, u8 offset);
int acpi_has_table(const char signature[4]);
int acpi_has_8042(void);

#endif

#ifndef MAYOS_AHCI_H
#define MAYOS_AHCI_H

#include "core.h"

int ahci_init(void);
u64 ahci_sector_count(void);
int ahci_read_sector(u64 lba, void *buffer);
int ahci_write_sector(u64 lba, const void *buffer);
int ahci_flush(void);

#endif

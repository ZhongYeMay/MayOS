#ifndef MAYOS_AHCI_H
#define MAYOS_AHCI_H

#include "core.h"

/* Returns the number of disks, or -1 if the scan could not complete. */
int ahci_init(void);
u64 ahci_sector_count(int index);
int ahci_read_sector(int index, u64 lba, void *buffer);
int ahci_write_sector(int index, u64 lba, const void *buffer);
int ahci_flush(int index);

#endif

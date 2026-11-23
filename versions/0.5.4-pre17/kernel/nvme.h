#ifndef MAYOS_NVME_H
#define MAYOS_NVME_H
#include "core.h"
/* Returns the number of namespaces, or -1 if the scan could not complete. */
int nvme_init(void);
u32 nvme_namespace_count(void);
u64 nvme_sector_count(int index);
int nvme_read_sector(int index, u64 lba, void *buffer);
int nvme_write_sector(int index, u64 lba, const void *buffer);
int nvme_flush(int index);
#endif

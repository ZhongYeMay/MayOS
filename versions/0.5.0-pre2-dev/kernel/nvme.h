#ifndef MAYOS_NVME_H
#define MAYOS_NVME_H
#include "core.h"
int nvme_init(void);
u64 nvme_sector_count(void);
int nvme_read_sector(u64 lba, void *buffer);
int nvme_write_sector(u64 lba, const void *buffer);
int nvme_flush(void);
#endif

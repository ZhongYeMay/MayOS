#ifndef MAYOS_MMIO_H
#define MAYOS_MMIO_H
#include "core.h"

/* The native x86-64 mapper treats BAR addresses as physical. DMA buffers
 * still use the existing low, identity-mapped allocator. */
volatile void *mmio_map(u64 physical, u32 bytes);

#endif

#ifndef MAYOS_PROCFS_H
#define MAYOS_PROCFS_H
/*
 * Shared MayOS desktop performance-data ABI, not a Linux /proc emulation.
 * All rates are per second, valid flags distinguish zero activity from N/A.
 * GPU load is invalid without a hardware performance-counter driver.
 */
#include "core.h"
typedef struct {
    u32 cpu_valid, cpu_percent;          /* MayKernel: non-idle TSC estimate */
    u32 ram_valid, ram_used_mib, ram_total_mib;
    u32 gpu_valid, gpu_percent;          /* never derive this from framebuffer */
    u32 framebuffer_valid, framebuffer_submits_s;
    u32 disk_valid, disk_busy_valid, disk_busy_percent;
    u32 disk_read_kib_s, disk_write_kib_s;
    u32 net_valid, net_link, net_rx_kib_s, net_tx_kib_s;
    u32 sample_ready, tick;
} MayPerfSnapshot;
void mayperf_sample(MayPerfSnapshot *snapshot, u32 now);
void mayperf_note_frame(void);
void mayperf_note_disk(int write, u32 bytes);
void mayperf_note_network(int transmit, u32 bytes);
#ifndef MAYOS_LINUX
void mayperf_note_idle_cycles(u64 cycles);
u32 mayperf_managed_pages(void);
u32 mayperf_used_pages(void);
#endif
#endif

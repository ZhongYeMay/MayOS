#ifndef MAYOS_PROCFS_H
#define MAYOS_PROCFS_H

#include "core.h"

#if !defined(MAYOS_LINUX) && defined(MAYOS_RUST_CORE)
#define MAYOS_PROCFS 1

typedef struct {
    u64 total_ticks, idle_ticks;
    u64 frames, disk_reads, disk_writes;
    u64 rx_bytes, tx_bytes;
    u32 managed_pages, free_pages, firmware_mb;
    u32 rx_packets, tx_packets, rx_errors, disk_present, net_present;
    char cpu[64], version[32];
} MayProcSnapshot;

typedef struct {
    int id, parent, directory;
    char name[16];
} MayProcEntry;

void mayos_proc_snapshot(MayProcSnapshot *snapshot);
const MayProcEntry *mayos_proc_entry(int id);
int mayos_proc_next(int parent, int after);
int mayos_proc_render(int id, u8 *buffer, u32 capacity);
void may_proc_io(MayProcSnapshot *snapshot);
#endif

typedef struct {
    const char *name;
    int parent, directory, read_only;
    u32 size;
} MayVfsNode;

typedef struct {
    u32 length, offset;
    u8 data[FILE_CAP];
} MayVfsReader;

void may_vfs_init(void);
int may_vfs_lookup(const char *path, int base);
int may_vfs_info(int node, MayVfsNode *info);
int may_vfs_next(int directory, int after);
int may_vfs_open(int node, MayVfsReader *reader);
u32 may_vfs_read(MayVfsReader *reader, u8 *buffer, u32 capacity);
int may_vfs_read_only(int node);
int fs_rename(int node, const char *name);

#endif

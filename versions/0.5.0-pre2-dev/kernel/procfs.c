/*
 * MayOS performance counters. The display consumes the same snapshot ABI in
 * freestanding MayKernel and the Linux desktop, with explicit N/A flags.
 * None of the five activity values are synthesized from a demo sequence.
 */
#ifdef MAYOS_LINUX
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#endif
#include "procfs.h"
#ifdef MAYOS_LINUX
/* The Linux desktop core.h redirects these helpers for freestanding GUI C;
 * this standalone collector intentionally calls the actual libc strcmp. */
#undef strcmp
#endif
#ifndef MAYOS_LINUX
#include "e1000.h"
#endif

static u64 frames_presented;
static u64 disk_read_bytes, disk_write_bytes;
static u64 network_rx_bytes, network_tx_bytes;
static u32 previous_tick;
static int previous_valid;
static u64 last_frames, last_disk_read, last_disk_write, last_net_rx, last_net_tx;
#ifndef MAYOS_LINUX
static volatile u64 idle_cycles;
static u64 previous_tsc, previous_idle;
static u64 tsc(void) {
    u32 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}
void mayperf_note_idle_cycles(u64 cycles) { idle_cycles += cycles; }
#else
static u64 previous_cpu_total, previous_cpu_idle;
static int cpu_initialized;
static u64 previous_linux_rx, previous_linux_tx;
static u64 previous_linux_read, previous_linux_write, previous_busy_ms;
static int network_initialized, disk_initialized;
#endif

void mayperf_note_frame(void) { frames_presented++; }
void mayperf_note_disk(int write, u32 bytes) {
    if (write) disk_write_bytes += bytes;
    else disk_read_bytes += bytes;
}
void mayperf_note_network(int transmit, u32 bytes) {
    if (transmit) network_tx_bytes += bytes;
    else network_rx_bytes += bytes;
}
static u32 rate_kib(u64 current, u64 last, u32 elapsed) {
    if (!elapsed || current < last) return 0;
    u64 value = (current - last) * 100u / elapsed / 1024u;
    return value > 0xffffffffu ? 0xffffffffu : (u32)value;
}
static u32 frames_per_second(u64 current, u64 last, u32 elapsed) {
    if (!elapsed || current < last) return 0;
    u64 value = (current - last) * 100u / elapsed;
    return value > 0xffffffffu ? 0xffffffffu : (u32)value;
}
#ifdef MAYOS_LINUX
static int linux_cpu(u64 *total, u64 *idle) {
    FILE *file = fopen("/proc/stat", "r");
    if (!file) return 0;
    char line[256] = {0};
    int success = fgets(line, sizeof(line), file) != 0;
    fclose(file);
    if (!success) return 0;
    unsigned long long value[8] = {0};
    int fields = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &value[0], &value[1], &value[2], &value[3],
        &value[4], &value[5], &value[6], &value[7]);
    if (fields < 4) return 0;
    *total = *idle = 0;
    for (int i = 0; i < fields; i++) *total += value[i];
    *idle = value[3] + (fields > 4 ? value[4] : 0);
    return 1;
}
static int linux_ram(u32 *used, u32 *total) {
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) return 0;
    char line[160];
    unsigned long long ram = 0, available = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: %llu kB", &ram) == 1) continue;
        if (sscanf(line, "MemAvailable: %llu kB", &available) == 1) continue;
    }
    fclose(file);
    if (!ram || available > ram) return 0;
    *total = (u32)(ram / 1024);
    *used = (u32)((ram - available) / 1024);
    return 1;
}
static int linux_net(u64 *rx, u64 *tx, u32 *link) {
    FILE *file = fopen("/proc/net/dev", "r");
    if (!file) return 0;
    char line[512];
    u64 all_rx = 0, all_tx = 0;
    int found = 0;
    while (fgets(line, sizeof(line), file)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        char *start = line;
        while (*start == ' ' || *start == '\t') start++;
        *colon = 0;
        if (!strcmp(start, "lo")) continue;
        unsigned long long col[16] = {0};
        int count = sscanf(colon + 1,
          "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
          &col[0], &col[1], &col[2], &col[3], &col[4], &col[5], &col[6], &col[7],
          &col[8], &col[9], &col[10], &col[11], &col[12], &col[13], &col[14], &col[15]);
        if (count != 16) continue;
        found = 1;
        all_rx += col[0];
        all_tx += col[8];
        char path[128];
        if (snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", start) > 0) {
            FILE *state = fopen(path, "r");
            if (state) {
                char word[32];
                if (fgets(word, sizeof(word), state) && !strncmp(word, "up", 2)) *link = 1;
                fclose(state);
            }
        }
    }
    fclose(file);
    *rx = all_rx; *tx = all_tx;
    return found;
}
static int physical_disk(const char *name) {
    if (!((name[0] == 's' && name[1] == 'd') ||
          (name[0] == 'h' && name[1] == 'd') ||
          (name[0] == 'v' && name[1] == 'd') ||
          !strncmp(name, "nvme", 4) || !strncmp(name, "mmcblk", 6))) return 0;
    char path[160];
    snprintf(path, sizeof(path), "/sys/class/block/%s/partition", name);
    return access(path, F_OK) != 0;
}
static int linux_disk(u64 *reads, u64 *writes, u64 *busy_ms) {
    FILE *file = fopen("/proc/diskstats", "r");
    if (!file) return 0;
    char line[512];
    u64 total_read = 0, total_write = 0, total_busy = 0;
    int found = 0;
    while (fgets(line, sizeof(line), file)) {
        unsigned major, minor;
        char name[64];
        unsigned long long v[11] = {0};
        int fields = sscanf(line,
            "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &major, &minor, name, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
            &v[6], &v[7], &v[8], &v[9], &v[10]);
        if (fields < 14 || !physical_disk(name)) continue;
        (void)major; (void)minor;
        found = 1;
        total_read += v[2] * 512u; total_write += v[6] * 512u;
        total_busy += v[9];
    }
    fclose(file);
    *reads = total_read; *writes = total_write; *busy_ms = total_busy;
    return found;
}
static int linux_gpu_percent(u32 *percentage) {
    /* Only an actual driver counter qualifies as hardware GPU utilization. */
    FILE *file = fopen("/sys/class/drm/card0/device/gpu_busy_percent", "r");
    if (!file) return 0;
    unsigned value;
    int ok = fscanf(file, "%u", &value) == 1 && value <= 100;
    fclose(file);
    if (ok) *percentage = value;
    return ok;
}
#endif

void mayperf_sample(MayPerfSnapshot *out, u32 now) {
    if (!out) return;
    /* Reset the entire snapshot so missing reads remain marked unavailable. */
    *out = (MayPerfSnapshot){0};
    out->tick = now;
    u32 elapsed = now - previous_tick;
    if (!previous_valid || !elapsed || elapsed > 6000) elapsed = 0;
    out->sample_ready = elapsed != 0;
    if (elapsed) {
        out->framebuffer_valid = 1;
        out->framebuffer_submits_s = frames_per_second(frames_presented, last_frames, elapsed);
    }
    last_frames = frames_presented;
#ifdef MAYOS_LINUX
    u64 cpu_total, cpu_idle;
    if (linux_cpu(&cpu_total, &cpu_idle)) {
        if (elapsed && cpu_initialized && cpu_total > previous_cpu_total) {
            u64 total = cpu_total - previous_cpu_total;
            u64 idle = cpu_idle >= previous_cpu_idle ? cpu_idle - previous_cpu_idle : total;
            out->cpu_percent = (u32)((total > idle ? (total-idle)*100u/total : 0u));
            out->cpu_valid = 1;
        }
        previous_cpu_total = cpu_total; previous_cpu_idle = cpu_idle; cpu_initialized = 1;
    } else cpu_initialized = 0;
    out->ram_valid = linux_ram(&out->ram_used_mib, &out->ram_total_mib);
    out->gpu_valid = linux_gpu_percent(&out->gpu_percent);
    u64 reads, writes, busy;
    if (linux_disk(&reads, &writes, &busy)) {
        out->disk_valid = 1;
        if (elapsed && disk_initialized) {
            out->disk_read_kib_s = rate_kib(reads, previous_linux_read, elapsed);
            out->disk_write_kib_s = rate_kib(writes, previous_linux_write, elapsed);
            if (busy >= previous_busy_ms) {
                u64 value = (busy - previous_busy_ms) * 10u / elapsed;
                out->disk_busy_percent = (u32)(value > 100 ? 100 : value);
                out->disk_busy_valid = 1;
            }
        }
        previous_linux_read = reads; previous_linux_write = writes;
        previous_busy_ms = busy; disk_initialized = 1;
    } else disk_initialized = 0;
    u64 rx, tx;
    if (linux_net(&rx, &tx, &out->net_link)) {
        out->net_valid = 1;
        if (elapsed && network_initialized) {
            out->net_rx_kib_s = rate_kib(rx, previous_linux_rx, elapsed);
            out->net_tx_kib_s = rate_kib(tx, previous_linux_tx, elapsed);
        }
        previous_linux_rx = rx; previous_linux_tx = tx; network_initialized = 1;
    } else network_initialized = 0;
#else
    /* Native CPU: measured non-HLT TSC fraction (approximation, not task load). */
    u64 cycles = tsc(), idle = idle_cycles;
    if (elapsed && previous_tsc && cycles > previous_tsc) {
        u64 total = cycles - previous_tsc;
        u64 asleep = idle >= previous_idle ? idle - previous_idle : total;
        if (asleep > total) asleep = total;
        out->cpu_percent = (u32)((total - asleep) * 100u / total);
        out->cpu_valid = 1;
    }
    previous_tsc = cycles; previous_idle = idle;
    u32 pages = mayperf_managed_pages();
    u32 used = mayperf_used_pages();
    if (pages && used <= pages) {
        out->ram_valid = 1;
        out->ram_total_mib = (pages + 255u) / 256u;
        out->ram_used_mib = used / 256u;
    }
    out->disk_valid = disk_ready ? 1u : 0u;
    if (elapsed && out->disk_valid) {
        out->disk_read_kib_s = rate_kib(disk_read_bytes, last_disk_read, elapsed);
        out->disk_write_kib_s = rate_kib(disk_write_bytes, last_disk_write, elapsed);
    }
    out->net_valid = e1000_mac_address() != 0;
    out->net_link = out->net_valid && e1000_link_up();
    if (elapsed && out->net_valid) {
        out->net_rx_kib_s = rate_kib(network_rx_bytes, last_net_rx, elapsed);
        out->net_tx_kib_s = rate_kib(network_tx_bytes, last_net_tx, elapsed);
    }
#endif
    last_disk_read = disk_read_bytes; last_disk_write = disk_write_bytes;
    last_net_rx = network_rx_bytes; last_net_tx = network_tx_bytes;
    previous_tick = now; previous_valid = 1;
}

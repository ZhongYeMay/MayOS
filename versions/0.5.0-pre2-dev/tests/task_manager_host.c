/* Host-side smoke test for the *actual* Linux /proc resource collector. */
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "../kernel/procfs.h"
int main(void) {
    MayPerfSnapshot initial, later;
    mayperf_sample(&initial, 100);
    assert(initial.ram_valid);
    assert(initial.ram_total_mib > 0);
    assert(initial.ram_used_mib <= initial.ram_total_mib);
    assert(initial.gpu_valid == 0 || initial.gpu_percent <= 100);
    mayperf_note_frame();
    sleep(1);
    mayperf_sample(&later, 200);
    assert(later.sample_ready);
    assert(later.framebuffer_valid && later.framebuffer_submits_s == 1);
    assert(later.ram_valid && later.ram_total_mib);
    assert(later.cpu_valid && later.cpu_percent <= 100);
    assert(later.gpu_valid == 0 || later.gpu_percent <= 100);
    assert(later.net_valid == 0 || later.net_link <= 1);
    printf("PASS: actual /proc/stat CPU=%u%%, RAM=%u/%u MiB, framebuffer=%u submissions/s, "
           "disk_valid=%u, net_valid=%u, hardware_gpu_valid=%u\n",
           later.cpu_percent, later.ram_used_mib, later.ram_total_mib,
           later.framebuffer_submits_s, later.disk_valid,
           later.net_valid, later.gpu_valid);
    return 0;
}

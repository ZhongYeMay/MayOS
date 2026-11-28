/* MayOS shared desktop frontend running as an ordinary Linux userspace process. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/perf_event.h>
#include <linux/kd.h>
#include <linux/reboot.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sound/asound.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "cursor.h"
#include "framebuffer.h"
#include "display_modes.h"
#include "wallpaper.h"
#include "task_manager.h"
#undef memset
#undef memcpy
#undef strlen
#undef strcmp
#include "network.h"

const u8 *platform_pointer_asset(u32 *size) {
    static u8 pointer_asset[MAY_CURSOR_FILE_MAX];
    static int attempted;
    static u32 length;
    if (!attempted) {
        attempted = 1;
        int fd = open("/mayos-pointer.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat info;
        if (fd >= 0) {
            if (!fstat(fd, &info) && S_ISREG(info.st_mode) &&
                info.st_size > 0 && info.st_size <= (off_t)sizeof(pointer_asset)) {
                while (length < (u32)info.st_size) {
                    ssize_t count = read(fd, pointer_asset + length, (u32)info.st_size - length);
                    if (count < 0 && errno == EINTR) continue;
                    if (count <= 0) { length = 0; break; }
                    length += (u32)count;
                }
            }
            close(fd);
        }
    }
    *size = length;
    return length ? pointer_asset : NULL;
}

void *mayos_memset(void *destination, int value, u32 length) {
    return memset(destination, value, length);
}
void *mayos_memcpy(void *destination, const void *source, u32 length) {
    /* The frontend scrolls its terminal buffer toward lower addresses. */
    return memmove(destination, source, length);
}
u32 mayos_strlen(const char *text) { return (u32)strlen(text); }
int mayos_strcmp(const char *left, const char *right) { return strcmp(left, right); }

volatile u32 ticks;
u32 memory_mb, free_pages;
char cpu_name[64];
static char kernel_label[96];
static int framebuffer_fd = -1, terminal_fd = -1, log_fd = -1, serial_fd = -1;
static u8 *framebuffer;
static size_t framebuffer_size;
static struct fb_fix_screeninfo fixed;
static struct fb_var_screeninfo variable;
static MaySurface display;
static int display_scale_mode;
static struct pollfd inputs[32];
static int input_count;
static struct timespec started;
static void *allocated_pages[64];
static int left_shift, right_shift, left_ctrl, right_ctrl, caps_lock;

static void write_text(int fd, const char *text) {
    if (fd < 0)
        return;
    size_t remaining = strlen(text);
    while (remaining) {
        ssize_t written = write(fd, text, remaining);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            break;
        text += written;
        remaining -= (size_t)written;
    }
}

void serial(const char *text) {
    write_text(log_fd, text);
    write_text(serial_fd, text);
    boot_splash_log(text);
}

#include "display_kms.h"
#ifdef MAYOS_INSTALLED_X11
#include "installed/x11.h"
#else
int platform_browser_ready(void) { return 0; }
int platform_browser_open(void) { return 0; }
int platform_browser_state(void) { return 0; }
void platform_browser_window_action(int close) { (void)close; }
#endif

static void startup_kernel_log(void) {
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    char record[4096];
    for (int i = 0; i < 1024; i++) {
        ssize_t length = read(fd, record, sizeof(record) - 1);
        if (length < 0 && (errno == EINTR || errno == EPIPE)) continue;
        if (length <= 0) break;
        record[length] = 0;
        char *message = strchr(record, ';');
        if (!message) continue;
        message++;
        message[strcspn(message, "\n")] = 0;
        boot_splash_log(message);
        boot_splash_log("\n");
    }
    close(fd);
}

static pid_t wallpaper_child;
static int wallpaper_enabled;
static u32 wallpaper_next;
static int wallpaper_read_exact(int fd, void *destination, size_t length) {
    unsigned char *bytes = destination;
    while (length) {
        ssize_t count = read(fd, bytes, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        bytes += count;
        length -= (size_t)count;
    }
    return 1;
}

int platform_wallpaper_poll(u32 *pixels, char date[9], int active) {
    int refresh = active && !wallpaper_enabled;
    wallpaper_enabled = active;
    if (refresh) wallpaper_next = ticks;
    if (wallpaper_child > 0) {
        int status;
        pid_t finished = waitpid(wallpaper_child, &status, WNOHANG);
        if (finished == 0 || (finished < 0 && errno == EINTR)) return 0;
        wallpaper_child = 0;
        int success = finished > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        wallpaper_next = ticks + (success ? 3600U : 300U) * 100U;
        refresh = active && success;
    }
    if (active && (i32)(ticks - wallpaper_next) >= 0) {
        char connection[160];
        if (!platform_network_status(connection, sizeof(connection))) {
            /* DHCP often completes just after the desktop becomes ready. */
            wallpaper_next = ticks + 300U;
        } else {
            wallpaper_child = fork();
            if (wallpaper_child == 0) {
                if (serial_fd >= 0) dup2(serial_fd, STDERR_FILENO);
                execl(MAY_WALLPAPER_WORKER, MAY_WALLPAPER_WORKER, (char *)NULL);
                _exit(127);
            }
            if (wallpaper_child < 0) wallpaper_next = ticks + 30000U;
        }
    }
    if (!refresh) return 0;

    int fd = open(MAY_WALLPAPER_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat info;
    char header[MAY_WALLPAPER_HEADER_BYTES];
    int ready = !fstat(fd, &info) && S_ISREG(info.st_mode) &&
        info.st_size == MAY_WALLPAPER_FILE_BYTES && wallpaper_read_exact(fd, header, sizeof(header)) &&
        !memcmp(header, MAY_WALLPAPER_MAGIC, 8) && wallpaper_date_valid(header + 8) &&
        memcmp(header + 8, date, 8) && wallpaper_read_exact(fd, pixels, MAY_WALLPAPER_IMAGE_BYTES);
    close(fd);
    if (!ready) return 0;
    memcpy(date, header + 8, 8);
    date[8] = 0;
    serial("MayOS: Bing daily wallpaper ready\n");
    return 1;
}

static void cleanup(void) {
#ifdef MAYOS_INSTALLED_X11
    x11_close();
#endif
    if (wallpaper_child > 0) kill(wallpaper_child, SIGTERM);
    for (int i = 0; i < input_count; i++) {
        ioctl(inputs[i].fd, EVIOCGRAB, 0);
        close(inputs[i].fd);
    }
    input_count = 0;
    kms_close();
    if (framebuffer && framebuffer != MAP_FAILED)
        munmap(framebuffer, framebuffer_size);
    framebuffer = NULL;
    if (terminal_fd >= 0) {
        ioctl(terminal_fd, KDSETMODE, KD_TEXT);
        close(terminal_fd);
        terminal_fd = -1;
    }
    if (framebuffer_fd >= 0) {
        close(framebuffer_fd);
        framebuffer_fd = -1;
    }
}

static void terminated(int signal_number) {
    cleanup();
    _exit(128 + signal_number);
}

static void fail(const char *message) {
    char text[280];
    snprintf(text, sizeof(text), "MayOS Linux desktop: FAILED: %s (%s)\n", message, strerror(errno));
    serial(text);
    fprintf(stderr, "%s", text);
    exit(1);
}

void copy(char *destination, const char *source, int capacity) {
    if (capacity <= 0)
        return;
    size_t length = strnlen(source, (size_t)capacity - 1);
    memcpy(destination, source, length);
    destination[length] = 0;
}

void number(char *buffer, u32 value) {
    snprintf(buffer, 12, "%u", value);
}

#ifdef MAYOS_LINUX_TASK_MANAGER
static u64 task_frames;

u64 may_task_frame_count(void) {
    return task_frames;
}

int may_task_read(const char *path, u8 *buffer, u32 capacity) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int used = 0;
    while ((u32)used < capacity) {
        ssize_t count = read(fd, buffer + used, capacity - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) used = -1;
        if (count <= 0) break;
        used += (int)count;
    }
    close(fd);
    return used;
}

/* i915 publishes a render-engine busy counter in nanoseconds via perf. */
int may_task_intel_render_busy(void) {
    static int fd = -1;
    static u64 before_busy, before_time;
    static u32 next_probe;
    if (fd < 0) {
        if (next_probe && (i32)(ticks - next_probe) < 0) return -1;
        next_probe = ticks + 3000;
        FILE *type_file = fopen("/sys/bus/event_source/devices/i915/type", "r");
        if (!type_file) return -1;
        unsigned type;
        int valid = fscanf(type_file, "%u", &type) == 1;
        fclose(type_file);
        if (!valid) return -1;
        FILE *event_file = fopen("/sys/bus/event_source/devices/i915/events/rcs0-busy", "r");
        if (!event_file) return -1;
        unsigned long long config;
        valid = fscanf(event_file, "config=%llx", &config) == 1;
        fclose(event_file);
        if (!valid) return -1;
        int cpu = 0;
        FILE *cpu_file = fopen("/sys/bus/event_source/devices/i915/cpumask", "r");
        if (cpu_file) {
            valid = fscanf(cpu_file, "%d", &cpu) == 1 && cpu >= 0;
            fclose(cpu_file);
            if (!valid) return -1;
        }
        struct perf_event_attr event = {.size = sizeof(event), .type = type, .config = config};
        fd = (int)syscall(SYS_perf_event_open, &event, -1, cpu, -1, PERF_FLAG_FD_CLOEXEC);
        if (fd < 0) return -1;
    }
    u64 busy;
    struct timespec now;
    if (read(fd, &busy, sizeof(busy)) != (ssize_t)sizeof(busy) ||
        clock_gettime(CLOCK_MONOTONIC, &now)) {
        close(fd);
        fd = -1;
        before_time = 0;
        return -1;
    }
    u64 time = (u64)now.tv_sec * 1000000000ULL + (u64)now.tv_nsec;
    int percent = -2;
    if (before_time && time > before_time && busy >= before_busy) {
        u64 elapsed = time - before_time, active = busy - before_busy;
        percent = active >= elapsed ? 100 : (int)(active * 100 / elapsed);
    }
    before_busy = busy;
    before_time = time;
    return percent;
}

u32 may_task_pids(u32 *pids, u32 capacity) {
    DIR *directory = opendir("/proc");
    if (!directory) return 0;
    struct dirent *entry;
    u32 count = 0;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') continue;
        char *end;
        unsigned long pid = strtoul(entry->d_name, &end, 10);
        if (*end || pid > INT32_MAX) continue;
        if (count < capacity) pids[count] = (u32)pid;
        count++;
    }
    closedir(directory);
    return count;
}

u32 may_task_page_size(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    return page_size > 0 ? (u32)page_size : 0;
}

int may_task_hardware_disk(u32 major, u32 minor) {
    char path[80];
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/partition", major, minor);
    if (access(path, F_OK) == 0) return 0;
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/device", major, minor);
    return access(path, F_OK) == 0;
}

int may_task_pidfd(u32 pid) {
    if (pid <= 1 || pid == (u32)getpid() || pid > INT32_MAX) return -1;
    return (int)syscall(SYS_pidfd_open, pid, 0);
}

int may_task_signal(int fd, int terminate) {
    int result = terminate ? (int)syscall(SYS_pidfd_send_signal, fd, SIGTERM, NULL, 0) : -1;
    close(fd);
    return result;
}
#endif

const char *platform_kernel(void) { return kernel_label; }

/* Fastfetch is an actual executable, never a rebranded built-in mock.
   Cache its output so repainting the framebuffer cannot repeatedly spawn it. */
int platform_fastfetch(char *destination, int capacity) {
    static char cached[2048];
    static int state = -1;
    if (capacity <= 0 || !destination)
        return 0;
    if (state < 0) {
        FILE *process = popen("/usr/bin/fastfetch --config none --pipe --logo none --structure os:kernel:cpu:memory:uptime 2>/dev/null", "r");
        state = 0;
        if (process) {
            size_t n = fread(cached, 1, sizeof(cached) - 1, process);
            cached[n] = 0;
            if (pclose(process) == 0 && n)
                state = 1;
        }
    }
    copy(destination, state ? cached : "", capacity);
    return state;
}

static void update_ticks(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t elapsed = (int64_t)(now.tv_sec - started.tv_sec) * 1000000000 + now.tv_nsec - started.tv_nsec;
    ticks = (u32)(elapsed / 10000000);
}

void platform_idle(void) {
    poll(inputs, (nfds_t)input_count, 10);
    update_ticks();
}

void *page_alloc(void) {
    for (unsigned int i = 0; i < sizeof(allocated_pages) / sizeof(allocated_pages[0]); i++) {
        if (!allocated_pages[i]) {
            void *page = NULL;
            if (posix_memalign(&page, 4096, 4096))
                return NULL;
            memset(page, 0, 4096);
            allocated_pages[i] = page;
            if (free_pages)
                free_pages--;
            return page;
        }
    }
    return NULL;
}

void page_free(void *page) {
    for (unsigned int i = 0; i < sizeof(allocated_pages) / sizeof(allocated_pages[0]); i++) {
        if (page && allocated_pages[i] == page) {
            allocated_pages[i] = NULL;
            free(page);
            free_pages++;
            return;
        }
    }
}

static int has_bit(const unsigned long *bits, unsigned int bit) {
    return (bits[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1;
}

static void open_input_devices(void) {
    int keyboards = 0, pointers = 0;
    /* PS/2 probing can finish after /init on fast BIOS and UEFI boots. */
    for (int attempt = 0; attempt < 100; attempt++) {
        DIR *directory = opendir("/dev/input");
        if (directory) {
            struct dirent *entry;
            while ((entry = readdir(directory)) && input_count < 32) {
                if (strncmp(entry->d_name, "event", 5))
                    continue;
                char path[300];
                snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
                int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0)
                    continue;
                unsigned long keys[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {0};
                ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
                int keyboard = has_bit(keys, KEY_A);
                int pointer = has_bit(keys, BTN_LEFT) || has_bit(keys, BTN_TOUCH);
                if (!keyboard && !pointer) {
                    close(fd);
                    continue;
                }
                /* A second open of an already grabbed device returns EBUSY. */
                if (ioctl(fd, EVIOCGRAB, 1) < 0) {
                    close(fd);
                    continue;
                }
                keyboards += keyboard;
                pointers += pointer;
                inputs[input_count++] = (struct pollfd){.fd = fd, .events = POLLIN};
                char text[360];
                snprintf(text, sizeof(text), "MayOS Linux desktop: evdev %s keyboard=%d pointer=%d\n", path, keyboard, pointer);
                serial(text);
            }
            closedir(directory);
        }
        if (keyboards && pointers)
            return;
        usleep(50000);
    }
    errno = ENODEV;
    fail("keyboard or pointer evdev device unavailable after 5 seconds");
}


/* Modes from /sys/class/drm/<connector>/modes are the kernel's connected
 * display/EDID modes. Prefer the first EDID timing, not a hardcoded 16:9 or
 * 4K value. The framebuffer may reject the mode switch (simpledrm/efifb);
 * in that case preserve the firmware-selected output and keep scaling. */
#define MAYOS_DISPLAY_MODES 24
static MayDisplayMode monitor_modes[MAYOS_DISPLAY_MODES];
static int monitor_mode_count;

static int mayos_drm_preferred_mode(MayDisplayMode *chosen, char *connector,
                                   size_t connector_size) {
    DIR *directory = opendir("/sys/class/drm");
    if (!directory) return 0;
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(directory))) {
        if (strncmp(entry->d_name, "card", 4) ||
            !strchr(entry->d_name, '-') ||
            strchr(entry->d_name, '/') ||
            strlen(entry->d_name) > 90)
            continue;
        char path[180];
        snprintf(path, sizeof(path), "/sys/class/drm/%s/status", entry->d_name);
        FILE *state = fopen(path, "r");
        if (!state) continue;
        char word[32] = {0};
        int connected = fgets(word, sizeof(word), state) &&
                        !strncmp(word, "connected", 9);
        fclose(state);
        if (!connected) continue;
        snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", entry->d_name);
        FILE *list = fopen(path, "r");
        if (!list) continue;
        /* Mode lists are usually small; parse each line rather than limiting
         * to an assumed set of video timings or aspect ratios. */
        char line[100];
        while (monitor_mode_count < MAYOS_DISPLAY_MODES && fgets(line, sizeof(line), list)) {
            MayDisplayMode mode;
            if (!may_display_parse(line, &mode)) continue;
            if (!found) {
                *chosen = mode;
                snprintf(connector, connector_size, "%s", entry->d_name);
                found = 1;
            }
            int duplicate = 0;
            for (int i = 0; i < monitor_mode_count; i++)
                if (monitor_modes[i].width == mode.width && monitor_modes[i].height == mode.height)
                    duplicate = 1;
            if (!duplicate) monitor_modes[monitor_mode_count++] = mode;
        }
        fclose(list);
        if (found) break; /* primary connected display, single FB desktop */
    }
    closedir(directory);
    return found;
}
static void mayos_choose_monitor_mode(void) {
    MayDisplayMode preferred;
    char connector[96] = {0};
    char message[256];
    if (!mayos_drm_preferred_mode(&preferred, connector, sizeof(connector))) {
        serial("MayOS display: no connected DRM/EDID mode; keeping firmware framebuffer\n");
        return;
    }
    snprintf(message, sizeof(message),
             "MayOS display: %s first supported monitor mode %ux%u (max dimension 4096)\n",
             connector, preferred.width, preferred.height);
    serial(message);
    if (variable.xres == preferred.width && variable.yres == preferred.height) {
        serial("MayOS display: already using preferred monitor resolution\n");
        return;
    }
    struct fb_var_screeninfo saved = variable;
    struct fb_var_screeninfo requested = variable;
    requested.xres = preferred.width;
    requested.yres = preferred.height;
    requested.xres_virtual = preferred.width;
    requested.yres_virtual = preferred.height;
    requested.xoffset = 0;
    requested.yoffset = 0;
    requested.activate = FB_ACTIVATE_NOW;
    if (ioctl(framebuffer_fd, FBIOPUT_VSCREENINFO, &requested) == 0) {
        struct fb_var_screeninfo actual;
        struct fb_fix_screeninfo updated;
        if (!ioctl(framebuffer_fd, FBIOGET_VSCREENINFO, &actual) &&
            !ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &updated) &&
            actual.xres == preferred.width && actual.yres == preferred.height) {
            variable = actual;
            fixed = updated;
            serial("MayOS display: monitor preferred mode activated\n");
            return;
        }
        /* Drivers may silently return a different geometry. Revert when it
         * differs, rather than treating it as successfully selected 4K. */
        ioctl(framebuffer_fd, FBIOPUT_VSCREENINFO, &saved);
    }
    if (ioctl(framebuffer_fd, FBIOGET_VSCREENINFO, &variable) ||
        ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &fixed))
        fail("read framebuffer after rejected monitor mode");
    serial("MayOS display: monitor mode switch unsupported; firmware mode retained\n");
}

static MaySurface make_surface(const struct fb_var_screeninfo *mode,
                               const struct fb_fix_screeninfo *device) {
    return (MaySurface){.width=mode->xres, .height=mode->yres,
        .pitch=device->line_length, .bpp=mode->bits_per_pixel,
        .xoffset=mode->xoffset, .yoffset=mode->yoffset,
        .red=mode->red.offset, .red_bits=mode->red.length,
        .green=mode->green.offset, .green_bits=mode->green.length,
        .blue=mode->blue.offset, .blue_bits=mode->blue.length,
        .alpha=mode->transp.offset, .alpha_bits=mode->transp.length};
}

int platform_display_has_modes(void) { return kms.count > 1; }

int platform_display_cycle(int direction) { return kms_start_trial(direction); }

int platform_display_finish(int keep) { return kms_finish_trial(keep); }

void hardware_init(u32 magic, u32 info) {
    (void)magic;
    (void)info;
#ifdef MAYOS_INSTALLED_X11
    log_fd = dup(STDERR_FILENO);
#else
    log_fd = open("/run/mayos-desktop.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
    serial_fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    atexit(cleanup);
    signal(SIGTERM, terminated);
    signal(SIGINT, terminated);
    clock_gettime(CLOCK_MONOTONIC, &started);
    struct utsname system;
    if (uname(&system))
        fail("uname");
    snprintf(kernel_label, sizeof(kernel_label), "Linux %.64s", system.release);
    struct sysinfo system_memory;
    if (sysinfo(&system_memory))
        fail("sysinfo");
    memory_mb = (u32)((uint64_t)system_memory.totalram * system_memory.mem_unit / 1048576);
    free_pages = (u32)((uint64_t)system_memory.freeram * system_memory.mem_unit / 4096);
    copy(cpu_name, "x86_64 / Linux userspace", sizeof(cpu_name));
    FILE *cpu = fopen("/proc/cpuinfo", "r");
    if (cpu) {
        char line[256];
        while (fgets(line, sizeof(line), cpu)) {
            if (!strncmp(line, "model name", 10)) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ')
                        colon++;
                    colon[strcspn(colon, "\r\n")] = 0;
                    copy(cpu_name, colon, sizeof(cpu_name));
                }
                break;
            }
        }
        fclose(cpu);
    }
#ifdef MAYOS_INSTALLED_X11
    if (!x11_open()) {
        errno = ENODEV;
        fail("connect to the installed X11 session");
    }
    boot_splash_begin("Linux64");
    boot_splash_step("Opening installed desktop");
    serial("MayOS Linux desktop: installed X11 backend ready\n");
    return;
#endif
    terminal_fd = open("/dev/tty1", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (terminal_fd < 0)
        fail("open tty1");
    if (ioctl(terminal_fd, VT_ACTIVATE, 1) || ioctl(terminal_fd, VT_WAITACTIVE, 1))
        fail("activate tty1");
    for (int attempt = 0; attempt < 100; attempt++) {
        framebuffer_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if (framebuffer_fd >= 0)
            break;
        usleep(50000);
    }
    if (framebuffer_fd < 0)
        fail("open /dev/fb0; boot with a graphics framebuffer");
    if (ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &fixed) || ioctl(framebuffer_fd, FBIOGET_VSCREENINFO, &variable))
        fail("framebuffer information");
    mayos_choose_monitor_mode();
    display = make_surface(&variable, &fixed);
    if (!surface_valid(&display, fixed.smem_len)) {
        errno = ENOTSUP;
        fail("requires firmware RGB framebuffer, at least 640x480, 16/24/32 bpp");
    }
    framebuffer_size = fixed.smem_len;
    uint64_t last_byte = (uint64_t)(variable.yoffset + variable.yres) * fixed.line_length;
    if (last_byte > framebuffer_size) {
        errno = EINVAL;
        fail("framebuffer size is smaller than visible rows");
    }
    framebuffer = mmap(NULL, framebuffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, framebuffer_fd, 0);
    if (framebuffer == MAP_FAILED)
        fail("mmap framebuffer");
    display.data = framebuffer;
    if (ioctl(terminal_fd, KDSETMODE, KD_GRAPHICS))
        fail("switch tty1 to graphics");
    kms_probe();
    startup_kernel_log();
    serial("MayOS Linux: framebuffer mapped\n");
    boot_splash_begin("Linux64");
    boot_splash_step("Opening input devices");
    open_input_devices();
    char text[256];
    snprintf(text, sizeof(text), "MayOS Linux desktop: backend ready; kernel=%s pid=%ld fb=%ux%ux%u driver=%.16s\n",
             system.release, (long)getpid(), variable.xres, variable.yres, variable.bits_per_pixel, fixed.id);
    serial(text);
    boot_splash_step("Starting MayOS session");
    FILE *pidfile = fopen("/run/mayos-desktop.pid", "w");
    if (pidfile) {
        fprintf(pidfile, "%ld\n", (long)getpid());
        fclose(pidfile);
    }
}

void present(u32 *pixels) {
#ifdef MAYOS_LINUX_TASK_MANAGER
    task_frames++;
#endif
#ifdef MAYOS_INSTALLED_X11
    if (x_display) { x11_present(pixels); return; }
#endif
    if (kms_trial >= 0 || scanout.fb) {
        kms_present(pixels);
        return;
    }
    if (display_scale_mode != 0 || display.width != W || display.height != H || display.bpp != 32 ||
        display.red_bits != 8 || display.green_bits != 8 || display.blue_bits != 8 ||
        (display.alpha_bits && display.alpha_bits != 8)) {
        surface_present(&display, pixels, display_scale_mode);
        return;
    }
    u32 x_origin = variable.xoffset + (variable.xres - W) / 2;
    u32 y_origin = variable.yoffset + (variable.yres - H) / 2;
    /*
     * The internal pixel layout is already 0x00RRGGBB. QEMU std VGA often
     * exposes exactly that XRGB8888 layout. A scanline copy avoids hundreds
     * of thousands of per-channel shifts every time the cursor moves.
     *
     * Keep the original conversion for BGR, alpha-bearing and other valid
     * framebuffer layouts. Both paths honor the actual line stride and
     * virtual framebuffer offsets validated during hardware_init().
     */
    int direct_copy = variable.red.offset == 16 && variable.green.offset == 8 &&
                      variable.blue.offset == 0 && variable.transp.length == 0;
    if (direct_copy) {
        for (u32 y = 0; y < H; y++) {
            void *destination = framebuffer + (y + y_origin) * fixed.line_length +
                                x_origin * sizeof(u32);
            memcpy(destination, pixels + y * W, W * sizeof(u32));
        }
        return;
    }
    u32 alpha = variable.transp.length == 8 && variable.transp.offset <= 24
                    ? 255u << variable.transp.offset : 0;
    for (u32 y = 0; y < H; y++) {
        u32 *destination = (u32 *)(framebuffer + (y + y_origin) * fixed.line_length) + x_origin;
        for (u32 x = 0; x < W; x++) {
            u32 color = pixels[y * W + x];
            destination[x] = ((color >> 16 & 255) << variable.red.offset) |
                             ((color >> 8 & 255) << variable.green.offset) |
                             ((color & 255) << variable.blue.offset) | alpha;
        }
    }
}

void display_set_scale_mode(int mode) {
    if (mode >= 0 && mode <= 3) display_scale_mode = mode;
}

void display_get_info(MayDisplayInfo *info) {
    const MaySurface *surface = kms_surface();
    info->width = surface->width; info->height = surface->height; info->bpp = surface->bpp;
    info->scale_mode = display_scale_mode;
    u32 horizontal = info->width * 100 / W;
    u32 vertical = info->height * 100 / H;
    info->scale_percent = horizontal < vertical ? horizontal : vertical;
    if (display_scale_mode == 1) info->scale_percent = 100;
    if (display_scale_mode == 3)
        info->scale_percent = horizontal > vertical ? horizontal : vertical;
    if (kms.fd >= 0) {
        const struct drm_mode_modeinfo *mode = kms_trial >= 0 ? &kms.modes[kms_trial]
            : scanout.fb ? &kms.modes[kms.selected] : &kms.original.mode;
        u64 total = (u64)mode->htotal * mode->vtotal * (mode->vscan > 1 ? mode->vscan : 1);
        info->refresh_hz = total ? (u32)((mode->clock * 1000ULL + total / 2) / total) : 0;
        info->refresh_known = info->refresh_hz != 0;
        return;
    }
    u64 total_x = (u64)variable.xres + variable.left_margin + variable.right_margin + variable.hsync_len;
    u64 total_y = (u64)variable.yres + variable.upper_margin + variable.lower_margin + variable.vsync_len;
    info->refresh_known = variable.pixclock && total_x && total_y;
    info->refresh_hz = info->refresh_known
        ? (u32)(1000000000000ULL / ((u64)variable.pixclock * total_x * total_y)) : 0;
}

static int keycode(unsigned int code, int value) {
    if (code == KEY_LEFTSHIFT) { left_shift = value != 0; return 0; }
    if (code == KEY_RIGHTSHIFT) { right_shift = value != 0; return 0; }
    if (code == KEY_LEFTCTRL) { left_ctrl = value != 0; return 0; }
    if (code == KEY_RIGHTCTRL) { right_ctrl = value != 0; return 0; }
    if (!value)
        return 0;
    if (code == KEY_CAPSLOCK) { if (value == 1) caps_lock = !caps_lock; return 0; }
    int shift = left_shift || right_shift;
    int extend = shift ? MAYOS_KEY_EXTEND : 0;
    switch (code) {
        case KEY_HOME: return MAYOS_KEY_HOME | extend;
        case KEY_END: return MAYOS_KEY_END | extend;
        case KEY_F1: return 0x105;
        case KEY_F2: return MAYOS_KEY_TASK_MANAGER;
        case KEY_F3: return MAYOS_KEY_IME;
        case KEY_UP: return 0x101 | extend;
        case KEY_DOWN: return 0x102 | extend;
        case KEY_LEFT: return MAYOS_KEY_LEFT | extend;
        case KEY_RIGHT: return MAYOS_KEY_RIGHT | extend;
        case KEY_DELETE: return 0x103;
        case KEY_LEFTMETA: case KEY_RIGHTMETA: return MAYOS_KEY_LAUNCHER;
        case KEY_KPENTER: return '\n';
        default: break;
    }
    static const char normal[] = "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[] = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (code >= sizeof(normal) - 1)
        return 0;
    int key = shift ? shifted[code] : normal[code];
    if (caps_lock && ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')))
        key ^= 32;
    if (left_ctrl || right_ctrl) return may_control_key(key, shift);
    return key;
}

/* ALSA control elements are selected by name and verified before writes. */
static int audio_card = -1, audio_level = -1;
static struct snd_ctl_elem_info audio_info;

static int mixer_open(int card) {
    char path[40];
    snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
    return open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
}

int platform_audio_volume(void) {
    if (audio_card >= 0) return audio_level;
    static const char *const candidates[] = {
        "Master Playback Volume", "Speaker Playback Volume", "PCM Playback Volume"
    };
    for (int card = 0; card < 8; card++) {
        int fd = mixer_open(card);
        if (fd < 0) continue;
        for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            struct snd_ctl_elem_info info = {0};
            info.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
            snprintf((char *)info.id.name, sizeof(info.id.name), "%s", candidates[i]);
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) ||
                info.type != SNDRV_CTL_ELEM_TYPE_INTEGER || !info.count || info.count > 128 ||
                !(info.access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
                info.value.integer.min < 0 || info.value.integer.max > INT32_MAX ||
                info.value.integer.min >= info.value.integer.max) continue;
            struct snd_ctl_elem_value value = {.id = info.id};
            if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &value)) continue;
            audio_info = info;
            audio_card = card;
            long minimum = info.value.integer.min, span = info.value.integer.max - minimum;
            audio_level = (int)((value.value.integer.value[0] - minimum) * 100 / span);
            close(fd);
            return audio_level;
        }
        close(fd);
    }
    return -1;
}

int platform_audio_set_volume(int level) {
    if (audio_card < 0 || level < 0 || level > 100) return 0;
    int fd = mixer_open(audio_card);
    if (fd < 0) return 0;
    struct snd_ctl_elem_value value = {.id = audio_info.id};
    long minimum = audio_info.value.integer.min, span = audio_info.value.integer.max - minimum;
    for (unsigned i = 0; i < audio_info.count; i++)
        value.value.integer.value[i] = minimum + (span * level + 50) / 100;
    int ok = ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &value) >= 0 &&
             !ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &value);
    if (ok) audio_level = (int)((value.value.integer.value[0] - minimum) * 100 / span);
    close(fd);
    return ok;
}

const u8 *platform_import_pack(u32 *size) {
    static u8 bytes[MAYOS_IMPORT_BYTES];
    *size = 0;
    int fd = open("/run/mayos-import.mfp", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > (off_t)sizeof(bytes)) {
        close(fd);
        return NULL;
    }
    size_t read_bytes = 0;
    while (read_bytes < (size_t)st.st_size) {
        ssize_t n = read(fd, bytes + read_bytes, (size_t)st.st_size - read_bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        read_bytes += (size_t)n;
    }
    close(fd);
    if (read_bytes != (size_t)st.st_size) return NULL;
    *size = (u32)read_bytes;
    return bytes;
}

int input_poll(int *mouse_x, int *mouse_y, int *buttons, int *key) {
    update_ticks();
    *key = 0;
#ifdef MAYOS_INSTALLED_X11
    if (x_display) return x11_input(mouse_x, mouse_y, buttons, key);
#endif
    int changed = 0;
    for (int i = 0; i < input_count; i++) {
        struct input_event event;
        int budget = 128;
        while (budget-- && read(inputs[i].fd, &event, sizeof(event)) == (ssize_t)sizeof(event)) {
            if (event.type == EV_REL) {
                if (event.code == REL_X) { *mouse_x += event.value; changed = 1; }
                if (event.code == REL_Y) { *mouse_y += event.value; changed = 1; }
            } else if (event.type == EV_ABS && (event.code == ABS_X || event.code == ABS_Y)) {
                struct input_absinfo range;
                if (!ioctl(inputs[i].fd, EVIOCGABS(event.code), &range) && range.maximum > range.minimum) {
                    /* Absolute input coordinates are physical monitor pixels.
                     * Apply the INVERSE of the actual Fit/Fill presentation,
                     * including 4:3 or ultrawide letterboxing offsets. */
                    int vertical = event.code == ABS_Y;
                    const MaySurface *surface = kms_surface();
                    u32 physical_extent = vertical ? surface->height : surface->width;
                    u32 physical = (u32)((int64_t)(event.value - range.minimum) *
                                         (physical_extent - 1) / (range.maximum - range.minimum));
                    int value = surface_screen_to_desktop(surface, display_scale_mode,
                                                          physical, vertical);
                    if (vertical) *mouse_y = value; else *mouse_x = value;
                    changed = 1;
                }
            } else if (event.type == EV_KEY) {
                int button = event.code == BTN_LEFT || event.code == BTN_TOUCH ? 1 :
                             event.code == BTN_RIGHT ? 2 : event.code == BTN_MIDDLE ? 4 : 0;
                if (button) {
                    if (event.value) *buttons |= button; else *buttons &= ~button;
                    changed = 1;
                } else {
                    *key = keycode(event.code, event.value);
                    if (*key)
                        return 1;
                }
            }
            if (*mouse_x < 0) *mouse_x = 0;
            if (*mouse_x >= W) *mouse_x = W - 1;
            if (*mouse_y < 0) *mouse_y = 0;
            if (*mouse_y >= H) *mouse_y = H - 1;
            if (event.type == EV_SYN && event.code == SYN_REPORT && changed)
                return 1;
        }
    }
    return changed;
}

void poweroff(void) {
    serial("MayOS Linux desktop: power off requested\n");
    sync();
    if (syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF, NULL) < 0)
        serial("MayOS Linux desktop: power off syscall failed\n");
}

void reboot(void) {
    serial("MayOS Linux desktop: reboot requested\n");
    sync();
    if (syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART, NULL) < 0)
        serial("MayOS Linux desktop: reboot syscall failed\n");
}

extern void kernel_main(u32 magic, u32 info);
int main(void) {
    kernel_main(0, 0);
    return 0;
}

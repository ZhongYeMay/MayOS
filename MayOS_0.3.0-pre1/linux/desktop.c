/* MayOS shared desktop frontend running as an ordinary Linux userspace process. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
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
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#undef memset
#undef memcpy
#undef strlen
#undef strcmp
#include "network.h"

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
static struct pollfd inputs[32];
static int input_count;
static struct timespec started;
static void *allocated_pages[64];
static int left_shift, right_shift, left_ctrl, right_ctrl, left_alt, right_alt, caps_lock;

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
}

static void cleanup(void) {
    for (int i = 0; i < input_count; i++) {
        ioctl(inputs[i].fd, EVIOCGRAB, 0);
        close(inputs[i].fd);
    }
    input_count = 0;
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

const char *platform_kernel(void) { return kernel_label; }
void platform_clock(char *buffer) {
    time_t now = time(NULL);
    struct tm result;
    if (localtime_r(&now, &result))
        snprintf(buffer, 8, "%02d:%02d", result.tm_hour, result.tm_min);
    else
        snprintf(buffer, 8, "--:--");
}


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

void hardware_init(u32 magic, u32 info) {
    (void)magic;
    (void)info;
    log_fd = open("/run/mayos-desktop.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
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
    if (variable.xres < W || variable.yres < H || variable.bits_per_pixel != 32 ||
        variable.red.length != 8 || variable.green.length != 8 || variable.blue.length != 8 ||
        variable.red.offset > 24 || variable.green.offset > 24 || variable.blue.offset > 24 ||
        fixed.line_length < variable.xres * 4 || !fixed.smem_len) {
        errno = ENOTSUP;
        fail("requires at least 1024x768, RGB888, 32-bit framebuffer");
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
    if (ioctl(terminal_fd, KDSETMODE, KD_GRAPHICS))
        fail("switch tty1 to graphics");
    memset(framebuffer, 0, framebuffer_size);
    open_input_devices();
    char text[256];
    snprintf(text, sizeof(text), "MayOS Linux desktop: backend ready; kernel=%s pid=%ld fb=%ux%ux%u driver=%.16s\n",
             system.release, (long)getpid(), variable.xres, variable.yres, variable.bits_per_pixel, fixed.id);
    serial(text);
    FILE *pidfile = fopen("/run/mayos-desktop.pid", "w");
    if (pidfile) {
        fprintf(pidfile, "%ld\n", (long)getpid());
        fclose(pidfile);
    }
}

void present(u32 *pixels) {
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

static int keycode(unsigned int code, int value) {
    if (code == KEY_LEFTSHIFT) { left_shift = value != 0; return 0; }
    if (code == KEY_RIGHTSHIFT) { right_shift = value != 0; return 0; }
    if (code == KEY_LEFTCTRL) { left_ctrl = value != 0; return 0; }
    if (code == KEY_LEFTALT) { left_alt = value != 0; return 0; }
    if (code == KEY_RIGHTALT) { right_alt = value != 0; return 0; }
    if (code == KEY_RIGHTCTRL) { right_ctrl = value != 0; return 0; }
    if (!value)
        return 0;
    if (code == KEY_CAPSLOCK) { if (value == 1) caps_lock = !caps_lock; return 0; }
    if (code == KEY_TAB && (left_alt || right_alt)) return 0x107;
    switch (code) {
        case KEY_F1: return 0x105;
        case KEY_UP: return 0x101;
        case KEY_DOWN: return 0x102;
        case KEY_DELETE: return 0x103;
        case KEY_LEFTMETA: case KEY_RIGHTMETA: return 0x104;
        case KEY_KPENTER: return '\n';
        default: break;
    }
    static const char normal[] = "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[] = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (code >= sizeof(normal) - 1)
        return 0;
    int shift = left_shift || right_shift;
    int key = shift ? shifted[code] : normal[code];
    if (caps_lock && ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z')))
        key ^= 32;
    if ((left_ctrl || right_ctrl) && (key == 's' || key == 'S'))
        return 0x106;
    if (left_ctrl || right_ctrl) {
        if (key == 'l' || key == 'L') return 0x108;
        if (key == 't' || key == 'T') return 0x109;
        if (key == 'w' || key == 'W') return 0x10a;
        if (key == 'f' || key == 'F') return 0x10b;
        if (key == 'r' || key == 'R') return 0x10c;
    }
    return key;
}

int input_poll(int *mouse_x, int *mouse_y, int *buttons, int *key) {
    update_ticks();
    *key = 0;
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
                    int value = (int)((int64_t)(event.value - range.minimum) *
                                     (event.code == ABS_X ? W - 1 : H - 1) / (range.maximum - range.minimum));
                    if (event.code == ABS_X) *mouse_x = value; else *mouse_y = value;
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

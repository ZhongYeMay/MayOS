#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "core.h"
#include "framebuffer.h"
#include "font.h"
#undef memset
#undef memcpy
#undef strlen
#undef strcmp

enum Page { WELCOME, DISKS, PROBE, ACTION, REVIEW, WORKING, FINISHED, FAILED };
enum { MAX_DISKS = 24, MAX_INPUTS = 32 };
static const u32 paper = 0xf5f5f6, muted_text = 0xb0b0b9, border = 0x35353c;
static u32 canvas[W * H];
static MaySurface surface;
static enum Page page = WELCOME;
static int x = 640, y = 360, disk_count, selected, action, step, installed, scanned;
static int sender, tty;
static char paths[MAX_DISKS][24], capacities[MAX_DISKS][20], status[80];
static const char *names[] = {"Install MayOS", "Stage an upgrade", "Confirm trial", "Discard trial"};
static const char *verbs[] = {"INSTALL", "UPGRADE", "CONFIRM", "DISCARD"};

static void rect(int left, int top, int w, int h, u32 color) {
    for (int py = top; py < top + h && py < H; py++) {
        if (py < 0) continue;
        for (int px = left; px < left + w && px < W; px++)
            if (px >= 0) canvas[py * W + px] = color;
    }
}

static void roundbox(int left, int top, int w, int h, int r, u32 color) {
    rect(left + r, top, w - 2 * r, h, color);
    rect(left, top + r, r, h - 2 * r, color);
    rect(left + w - r, top + r, r, h - 2 * r, color);
    for (int py = 0; py < r; py++)
        for (int px = 0; px < r; px++) {
            int dx = r - 1 - px, dy = r - 1 - py;
            if (dx * dx + dy * dy >= r * r) continue;
            rect(left + px, top + py, 1, 1, color);
            rect(left + w - 1 - px, top + py, 1, 1, color);
            rect(left + px, top + h - 1 - py, 1, 1, color);
            rect(left + w - 1 - px, top + h - 1 - py, 1, 1, color);
        }
}

static u32 blend(u32 background, u32 foreground, u32 a) {
    u32 color = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        u32 first = (background >> shift) & 255, second = (foreground >> shift) & 255;
        color |= ((first * (255 - a) + second * a) / 255) << shift;
    }
    return color;
}

static void text(int left, int top, const char *s, u32 color, int title) {
    int stride = title ? 36 : 28, height = title ? 40 : 30;
    for (; *s && left < 1036; s++) {
        int g = 0;
        while (g < PREMIUM_FONT_COUNT && premium_font_code[g] != (unsigned char)*s) g++;
        if (g == PREMIUM_FONT_COUNT) { left += 8; continue; }
        const u8 *bits = title ? premium_title_bitmap[g] : premium_body_bitmap[g];
        int width = title ? premium_title_advance[g] : premium_body_advance[g];
        for (int py = 0; py < height; py++)
            for (int px = 0; px < width + 2 && px < stride; px++) {
                u8 a = bits[py * stride + px];
                int index = (top + py) * W + left + px;
                if (a) canvas[index] = blend(canvas[index], color, a);
            }
        left += width;
    }
}

static void card(int left, int top, int w, int h, int chosen) {
    roundbox(left, top, w, h, 16, chosen ? 0xd4d4da : border);
    roundbox(left + 1, top + 1, w - 2, h - 2, 15, chosen ? 0x292930 : 0x16161b);
}

static int hit(int left, int top, int w, int h) {
    return x >= left && x < left + w && y >= top && y < top + h;
}

static int first_disk(void) { return selected >= 4 ? selected - 3 : 0; }

static void button(const char *label, int enabled) {
    roundbox(883, 530, 163, 46, 12, enabled ? 0xf4f4f6 : 0x33333a);
    text(900, 538, label, enabled ? 0x151519 : muted_text, 0);
}

static void title(const char *heading, const char *detail) {
    text(482, 145, heading, paper, 1);
    text(483, 201, detail, muted_text, 0);
}

static void paint(void) {
    for (int line = 0; line < H; line++)
        rect(0, line, W, 1, blend(0x050506, 0x16161e, (u32)line * 166 / H));
    for (int px = 0; px < W; px++) {
        int a = 131 - px / 3 + px * px / 4200;
        int b = 690 - px / 4 - (W - px) * (W - px) / 5000;
        for (int d = -7; d <= 7; d++) {
            int opacity = (7 - abs(d)) * 10;
            rect(px, a + d, 1, 1, blend(0x09090b, 0x575760, (u32)opacity));
            rect(px, b + d, 1, 1, blend(0x101015, 0x44444c, (u32)opacity));
        }
    }
    roundbox(175, 92, 930, 537, 30, 0x050506);
    roundbox(181, 87, 918, 530, 27, border);
    roundbox(182, 88, 916, 528, 26, 0x101013);
    rect(438, 109, 1, 478, 0x2c2c32);
    roundbox(215, 127, 25, 19, 4, paper);
    roundbox(217, 129, 21, 15, 3, 0x101013);
    rect(224, 147, 8, 2, paper);
    text(252, 111, "MayOS", paper, 1);
    text(214, 168, "System Installer", muted_text, 0);
    const char *steps[] = {"Welcome", "Destination", "Review", "Installation"};
    int active = page == WELCOME ? 0 : page <= PROBE ? 1 : page <= REVIEW ? 2 : 3;
    for (int i = 0; i < 4; i++) {
        int top = 228 + 55 * i;
        if (active == i) roundbox(207, top, 204, 45, 12, 0x28282e);
        roundbox(218, top + 14, 16, 16, 8, i <= active ? paper : 0x4b4b55);
        text(250, top + 7, steps[i], i <= active ? paper : muted_text, 0);
    }
    rect(482, 507, 564, 1, 0x2c2c32);
    if (page == WELCOME) {
        title("Install MayOS", "Install a new system or upgrade an existing disk.");
        card(482, 270, 564, 126, 0);
        text(509, 285, "MayKernel  0.5.1", paper, 1);
        text(510, 338, "Offline installation  /  x86 64 UEFI", muted_text, 0);
        button("Continue", 1);
    } else if (page == DISKS) {
        title("Choose a destination", scanned ? "Select the disk you want to use." : "Detecting disks");
        for (int i = first_disk(); i < disk_count && i < first_disk() + 4; i++) {
            int top = 249 + (i - first_disk()) * 58;
            card(482, top, 564, 51, i == selected);
            text(503, top + 10, paths[i], paper, 0);
            text(863, top + 10, capacities[i], muted_text, 0);
        }
        if (scanned && !disk_count) text(483, 285, "No supported target disks found", muted_text, 0);
        text(483, 538, "Back", muted_text, 0);
        button("Continue", disk_count && scanned);
    } else if (page == PROBE) {
        title("Checking destination", "Reading its partition layout");
    } else if (page == ACTION) {
        title("Choose an action", installed ? "An existing MayOS installation was found." : "A fresh MayOS installation is available.");
        int first = installed ? 1 : 0, end = installed ? 4 : 1;
        for (int i = first; i < end; i++) {
            int top = 251 + (i - first) * 69;
            card(482, top, 564, 58, i == action);
            text(504, top + 14, names[i], paper, 0);
        }
        if (installed) {
            const char *details[] = {"", "Start the new version once before confirming.",
                "Keep the version you already tested.", "Keep the currently confirmed version."};
            text(483, 463, details[action], muted_text, 0);
        }
        text(483, 538, "Back", muted_text, 0);
        button("Review", 1);
    } else if (page == REVIEW) {
        title(names[action], "Review your selection before continuing.");
        card(482, 255, 564, 116, 1);
        text(509, 268, paths[selected], paper, 1);
        text(510, 319, capacities[selected], muted_text, 0);
        text(483, 413, action == 0 ? "Only a completely empty disk can be installed."
                    : "Your MayFS files and settings will be kept.", muted_text, 0);
        text(483, 538, "Back", muted_text, 0);
        button(action == 0 ? "Install" : action == 1 ? "Stage update"
               : action == 2 ? "Confirm" : "Discard", 1);
    } else if (page == WORKING) {
        title(names[action], "Keep this machine powered on until the disk is verified.");
        roundbox(483, 302, 560, 10, 5, 0x303037);
        roundbox(483, 302, step == 2 ? 385 : 130, 10, 5, paper);
        text(483, 342, step == 2 ? "Writing and verifying" : "Checking the destination", paper, 0);
    } else if (page == FINISHED) {
        title(action == 0 ? "Installation complete" : "Boot selection updated",
              "Remove the installer ISO before restarting.");
        card(482, 270, 564, 118, 1);
        text(511, 289, action == 1 ? "Trial ready" : "Ready to restart", paper, 1);
        text(511, 341, action == 1 ? "Confirm after checking the trial desktop." : "Start MayOS from your hard disk.", muted_text, 0);
        button("Shut down", 1);
    } else {
        title("The action stopped", "Review the result before restarting.");
        card(482, 267, 564, 106, 0);
        text(503, 302, status, 0xffc3b6, 0);
        button("Shut down", 1);
    }
    for (int py = 0; py < 17; py++)
        for (int px = 0; px <= py / 2; px++)
            rect(x + px, y + py, 1, 1, px == 0 || px == py / 2 ? paper : 0x151519);
    surface_present(&surface, canvas, 0);
}

static void back(void) {
    if (page == DISKS) page = WELCOME;
    if (page == ACTION) {
        dprintf(sender, "BACK\n");
        scanned = 0;
        page = DISKS;
    }
    if (page == REVIEW) page = ACTION;
}

static void advance(void) {
    if (page == WELCOME) page = DISKS;
    else if (page == DISKS && scanned && disk_count) {
        dprintf(sender, "%s\n", paths[selected]);
        page = PROBE;
    } else if (page == ACTION) page = REVIEW;
    else if (page == REVIEW) {
        dprintf(sender, "%s %s\n", verbs[action], paths[selected]);
        step = 1;
        page = WORKING;
    } else if (page == FINISHED) dprintf(sender, "\n");
    else if (page == FAILED) exit(2);
}

static void event(const struct input_event *e) {
    if (e->type == EV_REL) {
        if (e->code == REL_X) x += e->value;
        if (e->code == REL_Y) y += e->value;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= W) x = W - 1;
        if (y >= H) y = H - 1;
    } else if (e->type == EV_KEY && e->value == 1) {
        if (e->code == KEY_ENTER || e->code == KEY_KPENTER) advance();
        else if (e->code == KEY_ESC || e->code == KEY_BACKSPACE) back();
        else if (page == DISKS && e->code == KEY_UP && selected > 0) selected--;
        else if (page == DISKS && e->code == KEY_DOWN && selected + 1 < disk_count) selected++;
        else if (page == ACTION && installed && e->code == KEY_UP && action > 1) action--;
        else if (page == ACTION && installed && e->code == KEY_DOWN && action < 3) action++;
        else if (e->code == BTN_LEFT) {
            if (hit(883, 530, 163, 46)) advance();
            else if (hit(482, 530, 112, 46)) back();
            else if (page == DISKS) {
                int start = first_disk();
                for (int i = start; i < disk_count && i < start + 4; i++)
                    if (hit(482, 249 + (i - start) * 58, 564, 51)) selected = i;
            } else if (page == ACTION && installed)
                for (int i = 1; i < 4; i++)
                    if (hit(482, 251 + (i - 1) * 69, 564, 58)) action = i;
        }
    }
}

static void read_message(const char *line) {
    if (!strncmp(line, "@DISK ", 6) && disk_count < MAX_DISKS) {
        if (sscanf(line + 6, "%23s %19s", paths[disk_count], capacities[disk_count]) == 2)
            disk_count++;
    } else if (!strcmp(line, "@SELECT")) scanned = 1;
    else if (!strncmp(line, "@MODE ", 6)) {
        installed = line[6] == '1';
        action = installed ? 1 : 0;
        page = ACTION;
    } else if (strstr(line, "Writing and verifying")) step = 2;
    else if (!strcmp(line, "@DONE")) page = FINISHED;
    else if (!strcmp(line, "Target contains data. No sectors were written."))
        snprintf(status, sizeof(status), "This disk already contains data.");
    else if (!strncmp(line, "@STOP ", 6)) {
        if (!status[0]) snprintf(status, sizeof(status), "%.53s", line + 6);
        page = FAILED;
    }
}

static int graphics(void) {
    int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    tty = open("/dev/tty1", O_RDWR | O_CLOEXEC);
    if (fd < 0 || tty < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) ||
        ioctl(fd, FBIOGET_VSCREENINFO, &var)) return 0;
    surface = (MaySurface){.width=var.xres, .height=var.yres, .pitch=fix.line_length,
        .bpp=var.bits_per_pixel, .xoffset=var.xoffset, .yoffset=var.yoffset,
        .red=var.red.offset, .red_bits=var.red.length, .green=var.green.offset,
        .green_bits=var.green.length, .blue=var.blue.offset, .blue_bits=var.blue.length,
        .alpha=var.transp.offset, .alpha_bits=var.transp.length};
    if (!surface_valid(&surface, fix.smem_len)) return 0;
    surface.data = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return surface.data != MAP_FAILED && !ioctl(tty, VT_ACTIVATE, 1) &&
           !ioctl(tty, KDSETMODE, KD_GRAPHICS);
}

static void restore(void) { ioctl(tty, KDSETMODE, KD_TEXT); }

int main(void) {
    if (!graphics()) return 1;
    atexit(restore);
    static struct pollfd inputs[MAX_INPUTS + 1];
    int count = 1;
    for (int i = 0; i < MAX_INPUTS; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) inputs[count++] = (struct pollfd){fd, POLLIN, 0};
    }
    if (count == 1) return 1;
    int input[2], output[2];
    if (pipe(input) || pipe(output)) return 1;
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) {
        close(input[1]); close(output[0]);
        dup2(input[0], 0); dup2(output[1], 1); dup2(output[1], 2);
        close(input[0]); close(output[1]);
        execl("/bin/sh", "sh", "/init", "--backend", NULL);
        _exit(127);
    }
    close(input[0]); close(output[1]);
    sender = input[1];
    signal(SIGPIPE, SIG_IGN);
    inputs[0] = (struct pollfd){output[0], POLLIN, 0};
    int serial_fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (serial_fd >= 0) dprintf(serial_fd, "MayOS installer graphical frontend ready\n");
    char line[512];
    size_t used = 0;
    paint();
    for (;;) {
        if (poll(inputs, (nfds_t)count, -1) < 0) continue;
        if (inputs[0].revents & (POLLIN | POLLHUP)) {
            char data[512];
            ssize_t length = read(output[0], data, sizeof(data));
            if (length <= 0) {
                waitpid(child, NULL, 0);
                return 2;
            }
            if (serial_fd >= 0 && write(serial_fd, data, (size_t)length) < 0) {
                close(serial_fd);
                serial_fd = -1;
            }
            for (ssize_t i = 0; i < length; i++) {
                if (data[i] == '\n') {
                    line[used] = 0;
                    read_message(line);
                    used = 0;
                } else if (used < sizeof(line) - 1) line[used++] = data[i];
            }
        }
        for (int i = 1; i < count; i++) {
            if (!(inputs[i].revents & POLLIN)) continue;
            struct input_event input_event;
            while (read(inputs[i].fd, &input_event, sizeof(input_event)) == sizeof(input_event))
                event(&input_event);
        }
        paint();
    }
}

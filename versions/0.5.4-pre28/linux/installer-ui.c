#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "design_palette.h"
#include "fluent_pointer.h"
#include "framebuffer.h"
#include "font.h"
#include "utf8.h"
#undef memset
#undef memcpy
#undef strlen
#undef strcmp

enum Page { WELCOME, DISKS, PROBE, ACTION, REVIEW, WORKING, FINISHED, FAILED };
enum { MAX_DISKS = 24, MAX_INPUTS = 32 };
static const u32 paper = MAY_DARK_INK, muted_text = MAY_DARK_MUTED, border = MAY_DARK_LINE;
static u32 canvas[W * H];
static MaySurface surface;
static enum Page page = WELCOME;
static int x = 640, y = 360, disk_count, selected, action, step, installed, scanned;
static int sender, tty;
static char paths[MAX_DISKS][24], capacities[MAX_DISKS][20], models[MAX_DISKS][32], status[80];
static unsigned percent;
static time_t stage_started;
static int chinese, nonblank;
#define I(en, zh) (chinese ? (zh) : (en))
static const char *action_title(int choice) {
    const char *names[] = {I("Install MayOS", "全新安装 MayOS"), I("Stage an upgrade", "安排试用升级"),
        I("Confirm trial", "确认试用版本"), I("Discard trial", "放弃试用版本")};
    return names[choice];
}
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

static int search_glyph(const unsigned short *code, int count, unsigned cp) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (code[mid] < cp) lo = mid + 1;
        else hi = mid;
    }
    return lo < count && code[lo] == cp ? lo : -1;
}

static int label_width(const char *s) {
    int width = 0;
    while (*s) {
        unsigned cp = may_utf8_next(&s);
        int glyph = search_glyph(premium_font_code, PREMIUM_FONT_COUNT, cp);
        width += glyph < 0 ? 16 : premium_body_advance[glyph];
    }
    return width;
}

static void text(int left, int top, const char *s, u32 color, int title) {
    for (; *s && left < 1046;) {
        unsigned cp = may_utf8_next(&s);
        int glyph = search_glyph(premium_font_code, PREMIUM_FONT_COUNT, cp);
        if (glyph >= 0) {
            int stride = title ? 36 : 28, height = title ? 40 : 30;
            const u8 *bits = title ? premium_title_bitmap[glyph] : premium_body_bitmap[glyph];
            int width = title ? premium_title_advance[glyph] : premium_body_advance[glyph];
            if (left + width > 1046) break;
            for (int py = 0; py < height; py++)
                for (int px = 0; px < width + 2 && px < stride; px++) {
                    u8 alpha = bits[py * stride + px];
                    int index = (top + py) * W + left + px;
                    if (alpha) canvas[index] = blend(canvas[index], color, alpha);
                }
            left += width;
        } else {
            glyph = search_glyph(font_code, FONT_COUNT, cp);
            int scale = title ? 2 : 1;
            if (left + 16 * scale > 1046) break;
            if (glyph < 0) glyph = 31;
            for (int py = 0; py < 20 * scale; py++)
                for (int px = 0; px < 16 * scale; px++) {
                    int index = (top + py + (title ? 3 : 6)) * W + left + px;
                    u8 alpha = font_bits[glyph][py / scale * 16 + px / scale];
                    if (alpha) canvas[index] = blend(canvas[index], color, alpha);
                }
            left += 16 * scale;
        }
    }
}

static void right_text(int right, int top, const char *s, u32 color) {
    text(right - label_width(s), top, s, color, 0);
}

static void card(int left, int top, int w, int h, int chosen) {
    roundbox(left, top, w, h, 16, chosen ? 0xd4d4da : border);
    roundbox(left + 1, top + 1, w - 2, h - 2, 15, chosen ? MAY_DARK_SELECTION : MAY_DARK_SURFACE);
}

static int hit(int left, int top, int w, int h) {
    return x >= left && x < left + w && y >= top && y < top + h;
}

static int first_disk(void) { return selected >= 4 ? selected - 3 : 0; }

static void button(const char *label, int enabled) {
    roundbox(883, 530, 163, 46, 12, enabled ? MAY_DARK_ACTION : 0x292931);
    text(883 + (163 - label_width(label)) / 2, 538, label, enabled ? MAY_DARK_ON_ACTION : muted_text, 0);
}

static void back_button(void) {
    roundbox(482, 530, 112, 46, 12, MAY_DARK_SURFACE);
    const char *label = I("Back", "上一步");
    text(482 + (112 - label_width(label)) / 2, 538, label, paper, 0);
}

static void title(const char *heading, const char *detail) {
    text(482, 145, heading, paper, 1);
    text(483, 201, detail, muted_text, 0);
}

static void paint(void) {
    for (int line = 0; line < H; line++)
        rect(0, line, W, 1, blend(MAY_DARK_BACKGROUND, 0x16161e, (u32)line * 166 / H));
    for (int px = 0; px < W; px++) {
        int a = 131 - px / 3 + px * px / 4200;
        int b = 690 - px / 4 - (W - px) * (W - px) / 5000;
        for (int d = -7; d <= 7; d++) {
            int opacity = (7 - abs(d)) * 10;
            rect(px, a + d, 1, 1, blend(0x09090b, 0x575760, (u32)opacity));
            rect(px, b + d, 1, 1, blend(0x101015, 0x44444c, (u32)opacity));
        }
    }
    roundbox(175, 92, 930, 537, 30, MAY_DARK_BACKGROUND);
    roundbox(181, 87, 918, 530, 27, border);
    roundbox(182, 88, 916, 528, 26, MAY_DARK_PANEL);
    rect(438, 109, 1, 478, MAY_DARK_LINE);
    roundbox(215, 127, 25, 19, 4, paper);
    roundbox(217, 129, 21, 15, 3, MAY_DARK_PANEL);
    rect(224, 147, 8, 2, paper);
    text(252, 111, "MayOS", paper, 1);
    text(214, 168, I("System Installer", "系统安装"), muted_text, 0);
    const char *steps[] = {I("Welcome", "开始"), I("Destination", "目标磁盘"),
                           I("Action", "安装方式"), I("Review", "核对"), I("Install", "执行")};
    int active = page == WELCOME ? 0 : page <= PROBE ? 1 : page == ACTION ? 2 : page == REVIEW ? 3 : 4;
    for (int i = 0; i < 5; i++) {
        int top = 224 + 51 * i;
        if (active == i) roundbox(207, top, 204, 43, 12, MAY_DARK_SELECTION);
        roundbox(219, top + 10, 23, 23, 8, i <= active ? 0xcacace : 0x29292e);
        char number[] = {(char)('1' + i), 0};
        text(225, top + 9, number, i <= active ? 0x18181c : muted_text, 0);
        text(253, top + 7, steps[i], i <= active ? paper : muted_text, 0);
    }
    text(215, 558, I("Offline recovery", "离线安装与恢复"), muted_text, 0);
    text(215, 583, "UEFI x86_64", muted_text, 0);

    if (page <= REVIEW) {
        card(883, 103, 163, 31, 0);
        text(895, 105, I("F4   中文", "F4   English"), paper, 0);
    }
    rect(482, 507, 564, 1, 0x303036);
    if (page == WELCOME) {
        title(I("Install MayOS", "安装 MayOS"),
              I("Set up a blank disk or service an existing installation.", "在空白磁盘上安装，或管理现有的 MayOS 系统。"));
        card(482, 266, 564, 91, 0);
        text(504, 275, I("New installation", "全新安装"), paper, 0);
        text(504, 312, I("Create the EFI system and MayFS partitions.", "在空白磁盘上创建 EFI 和 MayFS 分区。"), muted_text, 0);
        card(482, 370, 564, 91, 0);
        text(504, 379, I("Offline upgrade and recovery", "离线升级与恢复"), paper, 0);
        text(504, 416, I("Try a newer build and keep your MayFS data.", "试用新版系统，保留 MayFS 文件与设置。"), muted_text, 0);
        button(I("Next", "下一步"), 1);
    } else if (page == DISKS) {
        title(I("Choose a destination", "选择目标磁盘"),
              scanned ? I("Select the disk, then review the available actions.", "先选磁盘，再查看可用的安装方式。")
                      : I("Detecting disks", "正在检测磁盘"));
        for (int i = first_disk(); i < disk_count && i < first_disk() + 4; i++) {
            int top = 249 + (i - first_disk()) * 58;
            card(482, top, 564, 51, i == selected);
            text(503, top + (models[i][0] ? 0 : 10), paths[i], paper, 0);
            if (models[i][0]) text(503, top + 23, models[i], muted_text, 0);
            right_text(1025, top + 10, capacities[i], paper);
        }
        if (scanned && !disk_count)
            text(483, 287, I("No supported target disks found.", "没有找到可用的目标磁盘。"), muted_text, 0);
        back_button();
        button(I("Next", "下一步"), disk_count && scanned);
    } else if (page == PROBE) {
        title(I("Checking destination", "正在检查目标磁盘"),
              I("Reading the partition layout.", "正在读取分区布局。"));
        card(482, 278, 564, 80, 1);
        text(505, 302, paths[selected], paper, 0);
        right_text(1024, 302, capacities[selected], muted_text);
    } else if (page == ACTION) {
        title(I("Choose an action", "选择安装方式"),
              installed ? I("MayOS was found on this disk.", "已在目标磁盘上找到 MayOS。")
                        : I("Prepare a new MayOS installation.", "准备全新安装 MayOS。"));
        int first = installed ? 1 : 0, end = installed ? 4 : 1;
        for (int i = first; i < end; i++) {
            int top = 251 + (i - first) * 69;
            card(482, top, 564, 58, i == action);
            text(504, top + 14, action_title(i), paper, 0);
        }
        if (installed) {
            const char *details[] = {"", I("Start the new version once before confirming.", "确认升级前，先启动新版系统进行测试。"),
                I("Keep the version you already tested.", "将已试用的版本设为默认。"),
                I("Keep the currently confirmed version.", "继续使用当前已确认的版本。")};
            text(483, 463, details[action], muted_text, 0);
        } else {
            text(483, 330, I("The installer checks every sector before writing.", "安装器写入前会扫描目标磁盘的所有扇区。"), muted_text, 0);
        }
        back_button();
        button(I("Review", "核对设置"), 1);
    } else if (page == REVIEW) {
        title(action_title(action), I("Verify the destination before continuing.", "继续前请核对将要操作的目标磁盘。"));
        card(482, 255, 564, 116, 1);
        text(504, 267, I("Target disk", "目标磁盘"), muted_text, 0);
        text(504, 302, paths[selected], paper, 1);
        right_text(1024, 308, capacities[selected], paper);
        card(482, 389, 564, 82, 0);
        text(504, 396, action == 0 ? I("Blank disks only", "只允许空白磁盘")
                                  : I("MayFS data stays in place", "保留 MayFS 数据"), paper, 0);
        text(504, 431, action == 0 ? I("Any existing data stops the installation.", "发现已有数据时会停止安装。")
                                  : I("Only the boot selection and system slot are affected.", "此操作只涉及启动配置和系统槽位。"), muted_text, 0);
        if (action <= 1)
            text(504, 488, I("Allow a few minutes, plus time for any restart.",
                             "请预留几分钟，重启时间另计。"), muted_text, 0);
        back_button();
        button(action == 0 ? I("Install", "开始安装") : action == 1 ? I("Stage update", "安排试用")
               : action == 2 ? I("Confirm", "确认升级") : I("Discard", "放弃试用"), 1);
    } else if (page == WORKING) {
        title(action_title(action), I("Keep this machine powered on.", "请保持电脑开启，直到操作完成。"));
        card(482, 260, 564, 199, 0);
        text(506, 278, paths[selected], muted_text, 0);
        if (action <= 1) {
            const char *install_phases[] = {I("Waiting for disk verification", "等待磁盘检查"),
                I("Checking for existing data", "正在扫描目标磁盘"),
                I("Writing system files and partitions", "正在写入系统与分区"),
                I("Reading back and comparing", "正在回读校验")};
            const char *upgrade_phases[] = {I("Preparing trial slot", "准备试启动系统槽"),
                I("Verifying candidate", "正在验证更新"),
                I("Writing trial slot", "正在写入试启动系统"),
                I("Checking readback and scheduling trial", "正在校验并安排试启动")};
            text(506, 326, (action ? upgrade_phases : install_phases)[step], paper, 0);
            if (step) {
                char label[8];
                snprintf(label, sizeof(label), "%u%%", percent);
                right_text(1022, 326, label, paper);
                roundbox(506, 382, 516, 10, 5, 0x35353e);
                int width = 516 * (int)percent / 100;
                if (width >= 10) roundbox(506, 382, width, 10, 5, paper);
                else if (width) rect(506, 382, width, 10, paper);
                struct timespec now;
                if (step == 2 && percent > 0 && percent < 100 &&
                    clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec - stage_started >= 2) {
                    long seconds = (long)(now.tv_sec - stage_started) * (100 - percent) / percent;
                    char remaining[100];
                    snprintf(remaining, sizeof(remaining), I("Writing: about %ld s left", "预计还需 %ld 秒写完"),
                             seconds > 0 ? seconds : 1);
                    text(506, 410, remaining, muted_text, 0);
                }
            }
        } else {
            text(506, 326, I("Applying the selected boot action", "正在应用启动操作"), paper, 0);
        }
    } else if (page == FINISHED) {
        title(action == 0 ? I("Installation complete", "安装完成") : I("Boot selection updated", "启动配置已更新"),
              I("Remove the installer ISO before restarting.", "重新开机前，请移除安装镜像。"));
        card(482, 270, 564, 118, 1);
        text(511, 289, action == 1 ? I("Trial ready", "可以试启动") : I("Ready to restart", "可以重新开机"), paper, 1);
        text(511, 341, action == 1 ? I("Confirm after checking the trial desktop.", "检查试用系统后再确认升级。")
                                  : I("Start MayOS from your hard disk.", "接下来可从硬盘启动 MayOS。"), muted_text, 0);
        button(I("Shut down", "关机"), 1);
    } else {
        title(I("The action stopped", "操作已停止"), I("Review the result before restarting.", "重新开机前，请检查操作结果。"));
        card(482, 267, 564, 106, 0);
        if (nonblank)
            text(503, 302, I("The disk contains data. No sectors were written.", "目标磁盘已有数据，未写入任何扇区。"), 0xffc3b6, 0);
        else
            text(503, 302, status, 0xffc3b6, 0);
        button(I("Close", "关闭"), 1);
    }
    may_draw_system_pointer(canvas, x, y, 21, 0);
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
        step = 0;
        percent = 0;
        nonblank = 0;
        status[0] = 0;
        page = WORKING;
    } else if (page == FINISHED) dprintf(sender, "\n");
    else if (page == FAILED) exit(2);
}

static int absolute_position(int fd, unsigned axis, int value, int extent, int current) {
    struct input_absinfo range;
    if (ioctl(fd, EVIOCGABS(axis), &range) || range.maximum <= range.minimum)
        return current;
    if (value <= range.minimum) return 0;
    if (value >= range.maximum) return extent - 1;
    return (int)(((int64_t)value - range.minimum) * (extent - 1) /
                 ((int64_t)range.maximum - range.minimum));
}

static void event(int fd, const struct input_event *e) {
    if (e->type == EV_REL) {
        if (e->code == REL_X) x += e->value;
        if (e->code == REL_Y) y += e->value;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= W) x = W - 1;
        if (y >= H) y = H - 1;
    } else if (e->type == EV_ABS) {
        if (e->code == ABS_X) x = absolute_position(fd, ABS_X, e->value, W, x);
        if (e->code == ABS_Y) y = absolute_position(fd, ABS_Y, e->value, H, y);
    } else if (e->type == EV_KEY && e->value == 1) {
        if (e->code == KEY_F4 && page <= REVIEW) chinese = !chinese;
        else if (e->code == KEY_ENTER || e->code == KEY_KPENTER) advance();
        else if (e->code == KEY_ESC || e->code == KEY_BACKSPACE) back();
        else if (page == DISKS && e->code == KEY_UP && selected > 0) selected--;
        else if (page == DISKS && e->code == KEY_DOWN && selected + 1 < disk_count) selected++;
        else if (page == ACTION && installed && e->code == KEY_UP && action > 1) action--;
        else if (page == ACTION && installed && e->code == KEY_DOWN && action < 3) action++;
        else if (e->code == BTN_LEFT) {
            if (page <= REVIEW && hit(883, 103, 163, 31)) chinese = !chinese;
            else if (hit(883, 530, 163, 46)) advance();
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
        models[disk_count][0] = 0;
        if (sscanf(line + 6, "%23s %19s %31[^\n]", paths[disk_count],
                   capacities[disk_count], models[disk_count]) >= 2)
            disk_count++;
    } else if (!strcmp(line, "@SELECT")) scanned = 1;
    else if (!strncmp(line, "@MODE ", 6)) {
        installed = line[6] == '1';
        action = installed ? 1 : 0;
        page = ACTION;
    } else if (!strncmp(line, "@PROGRESS ", 10) && page == WORKING && action <= 1) {
        char phase[12], extra;
        unsigned value;
        if (sscanf(line + 10, "%11s %u %c", phase, &value, &extra) == 2 && value <= 100) {
            int next = !strcmp(phase, "scan") ? 1 : !strcmp(phase, "write") ? 2 : !strcmp(phase, "verify") ? 3 : 0;
            if (next > step || (next == step && value >= percent)) {
                if (next != step) {
                    struct timespec now;
                    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) stage_started = now.tv_sec;
                }
                step = next;
                percent = value;
            }
        }
    }
    else if (!strcmp(line, "@DONE")) page = FINISHED;
    else if (!strcmp(line, "Target contains data. No sectors were written."))
        nonblank = 1;
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

static int refresh_inputs(struct pollfd *devices, int serial_fd) {
    int count = 0;
    for (int i = 0; i < MAX_INPUTS; i++) {
        if (devices[i].fd < 0) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/input/event%d", i);
            devices[i].fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (devices[i].fd >= 0 && serial_fd >= 0) {
                unsigned char keys[KEY_MAX / 8 + 1] = {0};
                if (ioctl(devices[i].fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 &&
                    (keys[BTN_LEFT / 8] & (1 << (BTN_LEFT % 8))))
                    dprintf(serial_fd, "MayOS installer pointer ready\n");
            }
        }
        count += devices[i].fd >= 0;
    }
    return count;
}

int main(void) {
    if (!graphics()) return 1;
    atexit(restore);
    int serial_fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    static struct pollfd inputs[MAX_INPUTS + 1];
    for (int i = 1; i <= MAX_INPUTS; i++) inputs[i] = (struct pollfd){-1, POLLIN, 0};
    for (int attempt = 0; !refresh_inputs(inputs + 1, serial_fd); attempt++) {
        if (attempt == 10) return 1;
        usleep(500000);
    }
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
    if (serial_fd >= 0) dprintf(serial_fd, "MayOS installer graphical frontend ready\n");
    char line[512];
    size_t used = 0;
    paint();
    for (;;) {
        int ready = poll(inputs, MAX_INPUTS + 1, 250);
        if (ready < 0) continue;
        if (!ready) {
            // PS/2 probing and USB enumeration can finish after the initial scan.
            refresh_inputs(inputs + 1, serial_fd);
            continue;
        }
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
        for (int i = 1; i <= MAX_INPUTS; i++) {
            if (inputs[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                close(inputs[i].fd);
                inputs[i].fd = -1;
                continue;
            }
            if (!(inputs[i].revents & POLLIN)) continue;
            struct input_event input_event;
            while (read(inputs[i].fd, &input_event, sizeof(input_event)) == sizeof(input_event))
                event(inputs[i].fd, &input_event);
        }
        paint();
    }
}

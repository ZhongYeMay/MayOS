#include "cat_image.h"
#include "core.h"
#include "font.h"
#include "material_icons.h"
#include "browser_text.h"
#define MAYOS_RELEASE "0.4.0-pre3"
#ifdef MAYOS_LINUX
#define PLATFORM_SHORT "Linux / x86_64"
#define PLATFORM_BADGE "LINUX SESSION"
#define PLATFORM_ARCH "x86_64 / userspace"
#else
#define PLATFORM_SHORT "MayKernel / x86"
#define PLATFORM_BADGE "CUSTOM KERNEL"
#define PLATFORM_ARCH "x86 / 32-bit"
#endif
static u32 pixels[W * H];
/* Material Design 3 color roles, tuned for the 1024x768 framebuffer. */
static u32 bg = 0x141218, panel = 0x211f26, ink = 0xe6e1e5, muted = 0xcac4d0, line = 0x49454f,
           accent = 0xe2e2e7, surface_high = 0x2b2930, on_accent = 0x202124, selected_fill = 0x46474d;
static int dark = 1, accent_choice = 0;
static void rect(int x, int y, int w, int h, u32 c) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > W)
        w = W - x;
    if (y + h > H)
        h = H - y;
    if (w <= 0 || h <= 0)
        return;
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            pixels[j * W + i] = c;
}
static void roundrect(int x, int y, int w, int h, u32 c) {
    /* M3 medium-radius surface. No anti-aliasing or external dependencies. */
    int r = 10;
    if (w < 2 * r || h < 2 * r) {
        r = (w < h ? w : h) / 4;
    }
    if (r < 1) { rect(x, y, w, h, c); return; }
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    for (int yy = 0; yy < r; yy++)
        for (int xx = 0; xx < r; xx++) {
            int dx = r - xx - 1, dy = r - yy - 1;
            if (dx * dx + dy * dy <= r * r) {
                rect(x + xx, y + yy, 1, 1, c);
                rect(x + w - 1 - xx, y + yy, 1, 1, c);
                rect(x + xx, y + h - 1 - yy, 1, 1, c);
                rect(x + w - 1 - xx, y + h - 1 - yy, 1, 1, c);
            }
        }
}
static void circle(int cx, int cy, int r, u32 c) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                rect(cx + x, cy + y, 1, 1, c);
}
static int utf(const char **s) {
    const u8 *p = (const u8 *)*s;
    int c = *p++;
    if (c >= 0xe0) {
        c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63);
        p += 2;
    } else if (c >= 0xc0) {
        c = ((c & 31) << 6) | (p[0] & 63);
        p++;
    }
    *s = (const char *)p;
    return c;
}
static int glyph(int c) {
    int lo = 0, hi = FONT_COUNT - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (font_code[m] == c)
            return m;
        if (font_code[m] < c)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 31;
}
static u32 blend(u32 b, u32 f, u32 a) {
    u32 r = (((b >> 16 & 255) * (255 - a) + (f >> 16 & 255) * a) / 255),
        g = (((b >> 8 & 255) * (255 - a) + (f >> 8 & 255) * a) / 255),
        v = (((b & 255) * (255 - a) + (f & 255) * a) / 255);
    return r << 16 | g << 8 | v;
}
static int text(int x, int y, const char *s, u32 color) {
    int start = x;
    while (*s) {
        int c = utf(&s);
        if (c == '\n') {
            y += 22;
            x = start;
            continue;
        }
        int g = glyph(c), width = c < 128 ? 8 : 16;
        for (int j = 0; j < 20; j++)
            for (int i = 0; i < width; i++) {
                int px = x + i, py = y + j;
                if (px >= 0 && px < W && py >= 0 && py < H) {
                    u8 a = font_bits[g][j * 16 + i];
                    if (a)
                        pixels[py * W + px] = blend(pixels[py * W + px], color, a);
                }
            }
        x += width;
    }
    return x;
}
static int large_text(int x, int y, const char *s, u32 color) {
    while (*s) {
        int c = utf(&s), g = glyph(c), width = c < 128 ? 8 : 16;
        for (int j = 0; j < 20; j++)
            for (int i = 0; i < width; i++) {
                u8 a = font_bits[g][j * 16 + i];
                if (a)
                    for (int yy = 0; yy < 2; yy++)
                        for (int xx = 0; xx < 2; xx++) {
                            int px = x + i * 2 + xx, py = y + j * 2 + yy;
                            if (px >= 0 && px < W && py >= 0 && py < H)
                                pixels[py * W + px] = blend(pixels[py * W + px], color, a);
                        }
            }
        x += width * 2;
    }
    return x;
}
static void numtext(int x, int y, u32 n, u32 c) {
    char b[16];
    number(b, n);
    text(x, y, b, c);
}
static int inside(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}
/* Locally rasterized Material-style outline symbols. No emoji or icon font. */
static void stroke(int x0, int y0, int x1, int y1, int thickness, u32 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        rect(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}
static void ring(int cx, int cy, int r, int thickness, u32 c) {
    if (r < 1) return;
    int inner = r - thickness, rr = r * r, ii = inner * inner;
    for (int yy = -r; yy <= r; yy++)
        for (int xx = -r; xx <= r; xx++) {
            int d = xx * xx + yy * yy;
            if (d <= rr && (inner < 0 || d >= ii))
                rect(cx + xx, cy + yy, 1, 1, c);
        }
}
static void symbol(int x, int y, int kind, int size, u32 c) {
#if MAYOS_MATERIAL_ICONS
    if (kind >= 0 && kind < 20 && size > 0) {
        for (int j = 0; j < size; j++)
            for (int i = 0; i < size; i++) {
                int px = x + i, py = y + j;
                if (px >= 0 && px < W && py >= 0 && py < H) {
                    u8 opacity = material_icons[kind][(j * 48 / size) * 48 + i * 48 / size];
                    if (opacity)
                        pixels[py * W + px] = blend(pixels[py * W + px], c, opacity);
                }
            }
        return;
    }
#endif
    int cx = x + size / 2, cy = y + size / 2;
    int m = size / 5, thick = size >= 30 ? 3 : 2;
    if (kind == 0) {
        /* folder */
        stroke(x + m, y + m + 3, x + m + size / 3, y + m + 3, thick, c);
        stroke(x + m + size / 3, y + m + 3, x + m + size / 3 + size / 8, y + m + 7, thick, c);
        stroke(x + m, y + m + 3, x + m, y + size - m, thick, c);
        stroke(x + m, y + size - m, x + size - m, y + size - m, thick, c);
        stroke(x + size - m, y + m + 7, x + size - m, y + size - m, thick, c);
        stroke(x + m, y + m + 7, x + size - m, y + m + 7, thick, c);
    } else if (kind == 1) {
        /* settings */
        ring(cx, cy, size / 4, thick, c);
        ring(cx, cy, size / 9, thick, c);
        for (int n = -1; n <= 1; n += 2) {
            stroke(cx + n * size / 3, cy, cx + n * size / 4, cy, thick, c);
            stroke(cx, cy + n * size / 3, cx, cy + n * size / 4, thick, c);
        }
    } else if (kind == 2) {
        /* terminal */
        stroke(x + m, y + m, x + size - m, y + m, thick, c);
        stroke(x + size - m, y + m, x + size - m, y + size - m, thick, c);
        stroke(x + size - m, y + size - m, x + m, y + size - m, thick, c);
        stroke(x + m, y + size - m, x + m, y + m, thick, c);
        stroke(x + m + 4, cy - 4, x + m + 9, cy, thick, c);
        stroke(x + m + 9, cy, x + m + 4, cy + 4, thick, c);
        stroke(cx, cy + 6, cx + size / 5, cy + 6, thick, c);
    } else if (kind == 3) {
        /* delete */
        stroke(x + m, y + m + 4, x + size - m, y + m + 4, thick, c);
        stroke(cx - size / 6, y + m, cx + size / 6, y + m, thick, c);
        stroke(x + m + 3, y + m + 5, x + m + 5, y + size - m, thick, c);
        stroke(x + size - m - 3, y + m + 5, x + size - m - 5, y + size - m, thick, c);
        stroke(x + m + 5, y + size - m, x + size - m - 5, y + size - m, thick, c);
    } else if (kind == 4) {
        ring(cx, cy, size / 3, thick, c);
        stroke(x + m, cy, x + size - m, cy, thick, c);
        stroke(cx, y + m, cx, y + size - m, thick, c);
    } else if (kind == 5) {
        stroke(x + m, y + m, x + size - m, y + m, thick, c);
        stroke(x + m, y + m, x + m, y + size - m, thick, c);
        stroke(x + size - m, y + m, x + size - m, y + size - m, thick, c);
        stroke(x + m, y + size - m, x + size - m, y + size - m, thick, c);
        for (int row = 0; row < 3; row++)
            stroke(x + m + 5, y + m + 7 + row * (size / 7),
                   x + size - m - 5, y + m + 7 + row * (size / 7), thick, c);
    } else if (kind == 6) {
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++)
                roundrect(x + m + col * (size / 5),
                          y + m + row * (size / 5), size / 8, size / 8, c);
    } else if (kind == 7) {
        ring(cx, cy, size / 4, thick, c);
        rect(cx - size / 4, cy + size / 9, size / 2, thick, c);
        circle(cx, cy + size / 4 + thick, thick, c);
    } else if (kind == 8) {
        stroke(x + m, cy, cx, y + m, thick, c);
        stroke(cx, y + m, x + size - m, cy, thick, c);
        stroke(cx - size / 7, cy + 2, cx, cy - size / 8, thick, c);
        stroke(cx, cy - size / 8, cx + size / 7, cy + 2, thick, c);
        circle(cx, y + size - m, thick, c);
    } else {
        ring(cx, cy, size / 4, thick, c);
        stroke(cx, y + m, cx, cy - size / 4, thick, c);
        stroke(cx, cy + size / 4, cx, y + size - m, thick, c);
    }
}
static void icon(int x, int y, int kind, int size) {
    roundrect(x, y, size, size, surface_high);
    symbol(x, y, kind, size, ink);
}
static void button(int x, int y, int w, const char *s, int primary) {
    roundrect(x, y, w, 30, primary ? accent : surface_high);
    text(x + 12, y + 4, s, primary ? on_accent : ink);
}
typedef struct {
    int x, y, w, h, open, min, max, ox, oy, ow, oh;
    const char *title;
} Window;
#define WINDOW_COUNT 5
static Window windows[WINDOW_COUNT] = {{166, 100, 730, 510, 0, 0, 0, 0, 0, 0, 0, "文件 / Files"},
                            {186, 74, 800, 620, 1, 0, 0, 0, 0, 0, 0, "系统设置 / System Settings"},
                            {190, 150, 700, 448, 0, 0, 0, 0, 0, 0, 0, "终端 / Terminal"},
                            {240, 130, 650, 490, 0, 0, 0, 0, 0, 0, 0, "文本编辑器 / Editor"},
                            {180, 100, 800, 520, 0, 0, 0, 0, 0, 0, 0, "MayOS Explorer (Preview)"}};
static int order[WINDOW_COUNT] = {0, 2, 3, 4, 1}, focus = 1, dirty = 1;
static int mx = 530, my = 390, buttons, oldbuttons, drag = -1, dragx, dragy;
static int start_open = 0, context_open = 0, ctxx, ctxy, folder = 1, selected = -1, clip = -1,
           cut = 0, settings_tab = 0;
static int file_scroll = 0;
static int modal = 0, modal_target = -1;
static int egg_open = 0, egg_clicks = 0;
static u32 egg_last_click;
static void open_easter(void) {
    egg_open = 1;
    egg_clicks = 0;
    start_open = context_open = modal = 0;
    drag = -1;
    dirty = 1;
    serial("MayOS: meow easter egg opened\n");
}
static char modal_text[64], notice[120], search[48], device[48] = "MayOS Workstation";
static u32 notice_until = 0, last_click_tick;
static int notification_count;
static char notification_log[4][120];
static int resizing = -1;
static int last_clicked = -1;
static int editor_file = -1, editor_changed = 0;
static char editor_buf[FILE_CAP];
static char output[180][100], command[160], history[160];
static int out_count, cmdlen, cwd = 0, scroll = 0;
static void notify(const char *s) {
    copy(notice, s, sizeof(notice));
    for (int i = 3; i > 0; i--)
        copy(notification_log[i], notification_log[i - 1], sizeof(notification_log[i]));
    copy(notification_log[0], s, sizeof(notification_log[0]));
    if (notification_count < 3) notification_count++;
    notice_until = ticks + 350;
    dirty = 1;
}
static void raise_window(int id) {
    int at = 0;
    for (; at < WINDOW_COUNT && order[at] != id; at++)
        ;
    for (int i = at; i < WINDOW_COUNT - 1; i++)
        order[i] = order[i + 1];
    order[WINDOW_COUNT - 1] = id;
    windows[id].open = 1;
    windows[id].min = 0;
    focus = id;
    start_open = context_open = 0;
    dirty = 1;
}
static void emit(const char *s) {
    serial(s);
    serial("\n");
    while (*s) {
        if (out_count == 180) {
            memcpy(output, output + 1, 179 * 100);
            out_count--;
        }
        char *b = output[out_count++];
        int n = 0;
        while (*s && *s != '\n' && n < 82)
            b[n++] = *s++;
        b[n] = 0;
        if (*s == '\n')
            s++;
    }
    scroll = 0;
    dirty = 1;
}
#ifndef MAYOS_LINUX
static void emitnum(const char *label, u32 value, const char *tail) {
    char b[100], n[16];
    copy(b, label, 100);
    number(n, value);
    copy(b + strlen(b), n, 100 - strlen(b));
    copy(b + strlen(b), tail, 100 - strlen(b));
    emit(b);
}
#endif
static void terminal_init(void) {
    emit("MayOS Workstation " MAYOS_RELEASE " / MayShell");
    emit(platform_kernel());
    emit("Type help for commands. F1 opens this terminal.");
    emit(" ");
}
static int resolve(const char *p, int base) {
    char name[48];
    if (*p == '/') {
        base = 0;
        p++;
    }
    while (*p) {
        int n = 0;
        while (*p && *p != '/') {
            if (n < 47)
                name[n++] = *p;
            p++;
        }
        name[n] = 0;
        if (*p == '/')
            p++;
        if (!strcmp(name, ".."))
            base = files[base].parent;
        else if (strcmp(name, ".")) {
            base = fs_find(base, name);
            if (base < 0)
                return -1;
        }
        if (*p && !files[base].dir)
            return -1;
    }
    return base;
}
static int path_parent(const char *p, int *parent, char *name) {
    int n = strlen(p), split = -1;
    for (int i = 0; i < n; i++)
        if (p[i] == '/')
            split = i;
    *parent = cwd;
    if (split >= 0) {
        char prefix[160];
        copy(prefix, p, split + 2);
        *parent = resolve(prefix, cwd);
    }
    copy(name, p + split + 1, 48);
    return *parent >= 0 && files[*parent].dir && name[0];
}
static void pwd(void) {
    char path[180] = "", tmp[180];
    int chain[48], n = 0, a = cwd;
    while (a && n < 48) {
        chain[n++] = a;
        a = files[a].parent;
    }
    copy(path, "/", 180);
    while (n) {
        copy(tmp, files[chain[--n]].name, 180);
        copy(path + strlen(path), tmp, 180 - strlen(path));
        if (n)
            copy(path + strlen(path), "/", 180 - strlen(path));
    }
    emit(path);
}
static int sync_notice(void) {
    int ok = fs_sync();
#ifndef MAYOS_LINUX
    if (!ok)
        notify("磁盘不可写：修改仅保存在内存中");
#endif
    return ok;
}
static void shell(void) {
    char full[200] = "may@mayos:~ $ ";
    copy(full + 14, command, 186);
    emit(full);
    copy(history, command, 160);
    char buf[160];
    copy(buf, command, 160);
    char *argv[5];
    int argc = 0;
    char *p = buf;
    while (*p && argc < 5) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        if (argc == 3 && !strcmp(argv[0], "write"))
            break;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = 0;
    }
    if (!argc)
        return;
    char *c = argv[0];
    int target = argc > 1 ? resolve(argv[1], cwd) : -1;
    if (!strcmp(c, "help")) {
        emit("help  fastfetch  clear  pwd  ls [path]  cd <path>");
        emit("cat <file>  mkdir <path>  touch <path>  write <file> <text>");
        emit("mv <path> <new-name>  rm <path>  save  memtest");
        emit("theme light|dark  net status|ping|dns|renew  reboot  shutdown");
        emit("F1 opens Terminal. File names and input use ASCII keyboard.");
    } else if (!strcmp(c, "clear"))
        out_count = 0;
    else if (!strcmp(c, "pwd"))
        pwd();
    else if (!strcmp(c, "ls")) {
        int d = argc > 1 ? target : cwd;
        if (d < 0 || !files[d].dir)
            emit("ls: directory not found");
        else
            for (int i = 1; i < FS_MAX; i++)
                if (files[i].used && files[i].parent == (u32)d) {
                    char b[100];
                    copy(b, files[i].dir ? "[dir]  " : "       ", 100);
                    copy(b + 7, files[i].name, 93);
                    emit(b);
                }
    } else if (!strcmp(c, "cd")) {
        if (argc < 2)
            cwd = 0;
        else if (target >= 0 && files[target].dir)
            cwd = target;
        else
            emit("cd: directory not found");
    } else if (!strcmp(c, "cat")) {
        if (target >= 0 && !files[target].dir)
            emit((char *)files[target].data);
        else
            emit("cat: file not found");
    } else if (!strcmp(c, "mkdir") || !strcmp(c, "touch") || !strcmp(c, "write")) {
        int par;
        char name[48];
        if (argc < 2 || (!strcmp(c, "write") && argc < 3) || !path_parent(argv[1], &par, name)) {
            emit("usage: mkdir/touch <path> | write <file> <text>");
        } else {
            int i = fs_find(par, name);
            if (i >= 0 && (!strcmp(c, "mkdir") || files[i].dir)) {
                emit("Cannot create file: name already exists or is a directory");
            } else {
                if (i < 0)
                    i = fs_create(par, name, !strcmp(c, "mkdir"));
                if (i < 0)
                    emit("Cannot create file: invalid name or no space");
                else {
                    if (!strcmp(c, "write")) {
                        copy((char *)files[i].data, argv[2], FILE_CAP);
                        files[i].size = strlen((char *)files[i].data);
                    }
                    emit(sync_notice() ? "OK" : "OK (RAM only; disk sync unavailable)");
                }
            }
        }
    } else if (!strcmp(c, "rm")) {
        if (target < 4)
            emit("rm: missing path or protected system folder");
        else if (!fs_move(target, 3))
            emit("rm: cannot move to Trash (name already exists?)");
        else {
            sync_notice();
            emit("Moved to Trash");
        }
    } else if (!strcmp(c, "mv")) {
        if (argc < 3 || target < 4 || strlen(argv[2]) > 47 ||
            fs_find(files[target].parent, argv[2]) >= 0)
            emit("mv: invalid or duplicate name");
        else {
            int valid = 1;
            for (char *s = argv[2]; *s; s++)
                if (*s == '/')
                    valid = 0;
            if (!strcmp(argv[2], ".") || !strcmp(argv[2], ".."))
                valid = 0;
            if (valid) {
                copy(files[target].name, argv[2], 48);
                sync_notice();
                emit("Renamed");
            } else
                emit("mv: use a filename, without /");
        }
    } else if (!strcmp(c, "save")) {
#ifdef MAYOS_LINUX
        emit("RAM session: files are temporary; no disk is written.");
#else
        emit(fs_sync() ? "MayFS: synchronized to ATA disk" : "MayFS: disk unavailable");
#endif
    } else if (!strcmp(c, "fastfetch") || !strcmp(c, "neofetch")) {
#ifdef MAYOS_LINUX
        char result[1536];
        if (platform_fastfetch(result, sizeof(result))) {
            emit(result);
        } else {
            emit("Fastfetch is not installed in this live image.");
            emit("Rebuild Linux with a verified static --fastfetch binary.");
        }
#else
        emit("MayOS / native x86 kernel");
        emit("Fastfetch requires the Linux boot environment.");
        emit("OS: MayOS " MAYOS_RELEASE);
        emit("Kernel: MayKernel x86 (custom)");
        emit("Display: 1024 x 768 / 32 bpp");
        emitnum("Memory: ", memory_mb, " MiB");
        emitnum("Uptime: ", ticks / 100, " seconds");
        emit(disk_ready ? "Disk: ATA / MayFS" : "Disk: RAM fallback");
        emit(cpu_name);
#endif
    } else if (!strcmp(c, "net") || !strcmp(c, "network")) {
#ifdef MAYOS_LINUX
        char report[384];
        platform_network_status(report, sizeof(report));
        emit(report);
        emit("HTTP: Explorer supports http:// text preview; HTTPS unavailable.");
#else
        emit("Network: MayKernel has no native NIC / TCP/IP driver.");
        net_command(argc > 1 ? argv[1] : "status");
#endif
    } else if (!strcmp(c, "memtest")) {
        u32 before = free_pages;
        u32 *a = page_alloc(), *b = page_alloc();
        int ok = a && b && a != b;
        if (ok) {
            a[0] = 0x12345678;
            b[1023] = 0xaabbccdd;
            ok = a[0] == 0x12345678 && b[1023] == 0xaabbccdd;
        }
        if (a)
            page_free(a);
        if (b)
            page_free(b);
        emit(ok && before == free_pages ? "memtest: PASS (allocate, isolate, write, free)"
                                        : "memtest: FAIL");
    } else if (!strcmp(c, "meow")) {
        emit("meow! You found a little resident of MayOS.");
        open_easter();
    } else if (!strcmp(c, "theme") && argc > 1) {
        dark = !strcmp(argv[1], "dark");
    } else if (!strcmp(c, "reboot")) {
        fs_sync();
        reboot();
    } else if (!strcmp(c, "shutdown")) {
        fs_sync();
        poweroff();
    } else
        emit("Unknown command. Type help.");
}
static void open_file(int i) {
    if (i < 0)
        return;
    if (files[i].dir) {
        folder = i;
        file_scroll = 0;
        selected = -1;
        return;
    }
    if (editor_changed) {
        notify("请先保存编辑器中的修改");
        return;
    }
    editor_file = i;
    copy(editor_buf, (char *)files[i].data, FILE_CAP);
    editor_changed = 0;
    raise_window(3);
}
static void save_editor(void) {
    if (editor_file < 0 || !files[editor_file].used) {
        notify("文件已不存在");
        return;
    }
    copy((char *)files[editor_file].data, editor_buf, FILE_CAP);
    files[editor_file].size = strlen(editor_buf);
    int ok = fs_sync();
    editor_changed = disk_ready && !ok;
#ifdef MAYOS_LINUX
    notify("已保存到本次 RAM 桌面会话");
#else
    notify(ok           ? "已保存到 MayFS 磁盘"
           : disk_ready ? "保存失败：修改保留在内存中"
                        : "已保存到内存（磁盘未连接）");
#endif
}
static void begin_modal(int kind, int target) {
    modal = kind;
    modal_target = target;
    modal_text[0] = 0;
    if (kind == 3 && target >= 0)
        copy(modal_text, files[target].name, 64);
    if (kind == 4)
        copy(modal_text, device, 64);
    context_open = 0;
}
static void accept_modal(void) {
    if (modal >= 5) {
        modal = 0;
        return;
    }
    if (modal == 4) {
        if (modal_text[0])
            copy(device, modal_text, 48);
        modal = 0;
        return;
    }
    if (!modal_text[0]) {
        notify("请输入名称");
        return;
    }
    for (char *s = modal_text; *s; s++)
        if (*s == '/' || *s < ' ') {
            notify("名称不能包含 / 或控制字符");
            return;
        }
    if (!strcmp(modal_text, ".") || !strcmp(modal_text, "..")) {
        notify("名称不能为 . 或 ..");
        return;
    }
    if (strlen(modal_text) >= 48) {
        notify("名称最长 47 字节");
        return;
    }
    if (modal == 3) {
        if (modal_target < 4) {
            notify("系统文件夹不可重命名");
            return;
        }
        int duplicate = fs_find(folder, modal_text);
        if (duplicate >= 0 && duplicate != modal_target) {
            notify("同名文件已存在");
            return;
        }
        copy(files[modal_target].name, modal_text, 48);
    } else {
        int i = fs_create(folder, modal_text, modal == 2);
        if (i < 0) {
            notify("创建失败：同名文件或目录已满");
            return;
        }
        selected = i;
    }
    sync_notice();
    modal = 0;
}
static void palette(void) {
    const u32 dark_primary[] = {0xe2e2e7, 0xc7c8cf, 0xb7bbbd, 0xd4d1c9};
    const u32 light_primary[] = {0x414249, 0x505257, 0x5d6264, 0x69675d};
    bg = dark ? 0x141218 : 0xfffbfe;
    panel = dark ? 0x211f26 : 0xfff7ff;
    surface_high = dark ? 0x2b2930 : 0xf3edf7;
    ink = dark ? 0xe6e1e5 : 0x1d1b20;
    muted = dark ? 0xcac4d0 : 0x49454f;
    line = dark ? 0x49454f : 0xcac4d0;
    accent = dark ? dark_primary[accent_choice] : light_primary[accent_choice];
    on_accent = dark ? 0x202124 : 0xffffff;
    selected_fill = dark ? 0x46474d : 0xe0e1e5;
}
static int quick_open, notifications_open, overview_open, desktop_context_open, wallpaper_mode, clock_open, power_open;
static int brightness_level = 100, desktop_x, desktop_y;
static int desktop_icon_at(int px, int py) {
    int item = 0;
    for (int i = 4; i < FS_MAX; i++) {
        if (!files[i].used || files[i].parent != 2) continue;
        int x = 125 + (item % 5) * 124;
        int y = 96 + (item / 5) * 100;
        if (inside(px, py, x, y, 104, 90)) return i;
        item++;
        if (item >= 15) break;
    }
    return -1;
}
static void desktop(void) {
    /* macOS placement, Material 3 neutral tonal surfaces.
       Actual status actions are icon-only; textual menus remain at left. */
    for (int y = 0; y < H; y++) {
        u32 top = dark ? 0x17181b : 0xf3f3f5;
        u32 bottom = dark ? 0x292a2e : 0xdfe1e5;
        rect(0, y, W, 1, blend(top, bottom, (u32)y * 140 / H));
    }
    /* Quiet geometric detail: architectural lines rather than illustrations. */
    for (int i = 0; i < 5; i++)
        rect(230 + i * 75, 155 + i * 38, 1, 336, dark ? 0x33343a : 0xd4d6da);
    rect(0, 0, W, 42, dark ? 0x211f23 : 0xf6f6f8);
    rect(0, 40, W, 1, dark ? 0x38393f : 0xd8d9dc);
    rect(0, 41, W, 2, dark ? 0x111215 : 0xc8c9cd);
    text(24, 10, "MayOS", ink);
    text(126, 10, "文件", muted);
    text(200, 10, "窗口", muted);
    text(274, 10, "应用", muted);
    text(348, 10, "终端", muted);
#ifdef MAYOS_LINUX
    char state[180];
    int online = platform_network_status(state, sizeof(state));
#else
    int online = net_online();
#endif
    const int icons[] = {10, online ? 8 : 13, 9, 7, 12, 1};
    for (int i = 0; i < 6; i++) {
        int x = 706 + i * 50;
        int active = (i == 0 && clock_open) || (i == 1 && quick_open) ||
                     (i == 2 && quick_open) || (i == 3 && notifications_open) ||
                     (i == 4 && power_open) || (i == 5 && quick_open);
        int hover = inside(mx, my, x, 3, 42, 36);
        if (active || hover)
            roundrect(x, 3, 42, 35, active ? selected_fill : surface_high);
        symbol(x + 9, 9, icons[i], 24,
               i == 1 && !online ? muted : active ? ink : muted);
    }
    if (notification_count)
        circle(887, 7, 3, accent);
    int count = 0;
    for (int i = 4; i < FS_MAX && count < 15; i++) {
        if (!files[i].used || files[i].parent != 2) continue;
        int x = 125 + (count % 5) * 124;
        int y = 96 + (count / 5) * 100;
        int hover = inside(mx, my, x, y, 104, 90);
        if (selected == i || hover)
            roundrect(x + 12, y - 3, 68, 79,
                      selected == i ? selected_fill : (dark ? 0x38393c : 0xe7e8ea));
        icon(x + 21, y, files[i].dir ? 0 : 5, 50);
        text(x, y + 58, files[i].name, dark ? 0xf3f2f4 : 0x27282b);
        count++;
    }
}
static void frame(int id) {
    Window *w = &windows[id];
    /* Tonal window elevation with layered neutral shadows. */
    roundrect(w->x + 6, w->y + 9, w->w, w->h,
              dark ? 0x0d0e10 : 0xc4c6cc);
    roundrect(w->x + 2, w->y + 3, w->w, w->h,
              dark ? 0x17181d : 0xd5d7db);
    roundrect(w->x, w->y, w->w, w->h, panel);
    roundrect(w->x + 1, w->y + 1, w->w - 2, 44, surface_high);
    rect(w->x + 1, w->y + 34, w->w - 2, 10, surface_high);
    rect(w->x + 1, w->y + 43, w->w - 2, 1, line);
    symbol(w->x + 16, w->y + 11,
           id == 0 ? 0 : id == 1 ? 1 : id == 4 ? 4 : id == 3 ? 5 : 2,
           22, ink);
    text(w->x + 48, w->y + 11, w->title, focus == id ? ink : muted);
    int controls = w->x + w->w - 119;
    const int kinds[] = {15, 16, 14};
    for (int i = 0; i < 3; i++) {
        int x = controls + i * 36 + 1;
        int hover = inside(mx, my, x, w->y + 5, 33, 33);
        roundrect(x, w->y + 5, 33, 33,
                  hover ? selected_fill : surface_high);
        symbol(x + 6, w->y + 11, kinds[i], 20,
               hover ? ink : muted);
    }
    for (int i = 0; i < 3; i++)
        stroke(w->x + w->w - 8 - i * 5, w->y + w->h - 4,
               w->x + w->w - 4, w->y + w->h - 8 - i * 5, 1, muted);
}
static void settings(Window *w) {
    int x = w->x, y = w->y, cx = x + 204, cy = y + 65, cw = w->w - 234;
    rect(x + 1, y + 43, 176, w->h - 44, bg);
    rect(x + 176, y + 43, 1, w->h - 44, line);
    text(x + 24, y + 64, "WORKSTATION", muted);
    const char *tabs[] = {"关于系统", "外观设置", "存储空间", "开源组件"};
    const char *marks[] = {"01", "02", "03", "04"};
    for (int i = 0; i < 4; i++) {
        int yy = y + 106 + i * 46;
        if (settings_tab == i) {
            roundrect(x + 12, yy, 152, 36, selected_fill);
            rect(x + 12, yy + 8, 2, 20, accent);
        }
        text(x + 25, yy + 7, marks[i], settings_tab == i ? accent : muted);
        text(x + 55, yy + 7, tabs[i], settings_tab == i ? ink : muted);
    }
    rect(x + 22, y + w->h - 126, 132, 1, line);
    text(x + 24, y + w->h - 106, "帮助 / Help", accent);
    text(x + 24, y + w->h - 62, MAYOS_RELEASE, muted);
    text(x + 24, y + w->h - 37, PLATFORM_ARCH, muted);
    if (settings_tab == 0) {
        text(cx, cy, "SYSTEM / ABOUT", accent);
        large_text(cx, cy + 23, "MayOS", ink);
        roundrect(cx + 111, cy + 32, 161, 25, dark ? 0x26433f : 0xdeeeea);
        text(cx + 124, cy + 35, MAYOS_RELEASE, accent);
        text(cx, cy + 72, "系统 > 关于 / " PLATFORM_SHORT, muted);
        roundrect(cx, cy + 105, cw, 90, bg);
        icon(cx + 16, cy + 125, 1, 44);
        text(cx + 78, cy + 117, device, ink);
        text(cx + 78, cy + 146, platform_kernel(), accent);
        text(cx + 78, cy + 170, PLATFORM_ARCH, muted);
#ifdef MAYOS_LINUX
        text(cx, cy + 217, "FASTFETCH / SYSTEM", ink);
        rect(cx, cy + 243, cw, 1, line);
        char fastfetch_lines[1152];
        if (platform_fastfetch(fastfetch_lines, sizeof(fastfetch_lines))) {
            char *start = fastfetch_lines;
            for (int n = 0; n < 5 && *start; n++) {
                char *end = start;
                while (*end && *end != '\n')
                    end++;
                char terminator = *end;
                *end = 0;
                text(cx, cy + 254 + n * 20, start, ink);
                if (!terminator)
                    break;
                start = end + 1;
            }
        } else {
            text(cx, cy + 266, "Fastfetch not bundled in this Linux image.", muted);
            text(cx, cy + 294, "Rebuild with --fastfetch /path/to/fastfetch.", muted);
        }
#else
        text(cx, cy + 217, "硬件与运行环境", ink);
        rect(cx, cy + 243, cw, 1, line);
        char shortcpu[49];
        copy(shortcpu, cpu_name, sizeof(shortcpu));
        text(cx, cy + 254, "处理器", muted);
        text(cx + 94, cy + 254, shortcpu, ink);
        text(cx, cy + 284, "内存", muted);
        numtext(cx + 94, cy + 284, memory_mb, ink);
        text(cx + 143, cy + 284, "MiB / 实际检测", muted);
        text(cx, cy + 314, "显示", muted);
        text(cx + 94, cy + 314, "1024 × 768 / 32-bit", ink);
#endif
        rect(cx, cy + 346, cw, 1, line);
        text(cx, cy + 357, "MayOS Team 2023 - 2026.", ink);
        text(cx, cy + 383, "This version of MayOS is for test use only.", 0xd5b583);
        text(cx, cy + 411, "第三方组件与许可证请查看“开源组件”。", muted);
        button(cx, cy + 449, 126, "重命名设备", 0);
        button(cx + 140, cy + 449, 194, "开源组件 / Credits", 0);
        text(cx, cy + 505, "Desktop session / experimental release", muted);
    } else if (settings_tab == 1) {
        text(cx, cy, "PERSONALIZATION", accent);
        large_text(cx, cy + 26, "外观设置", ink);
        text(cx, cy + 76, "调整窗口与主题色，工作区始终保持清晰。", muted);
        text(cx, cy + 120, "窗口外观", ink);
        for (int i = 0; i < 2; i++) {
            int xx = cx + i * 236;
            roundrect(xx, cy + 155, 216, 128, i ? 0x0e1b26 : 0xe3eaed);
            roundrect(xx + 18, cy + 171, 180, 87, i ? 0x18232e : 0xffffff);
            rect(xx + 18, cy + 171, 180, 19, i ? 0x2b3a46 : 0xd5e1e5);
            rect(xx + 18, cy + 190, 40, 68, i ? 0x101924 : 0xf0f4f5);
            rect(xx + 71, cy + 208, 90, 4, i ? 0x506978 : 0xc0d0d6);
            rect(xx + 71, cy + 222, 111, 4, i ? 0x324b59 : 0xd8e2e6);
            text(xx, cy + 297, i ? "深色 / Dark" : "浅色 / Light", ink);
            if (dark == i) {
                circle(xx + 201, cy + 306, 8, accent);
                circle(xx + 201, cy + 306, 3, dark ? 0x13232b : 0xffffff);
            }
        }
        text(cx, cy + 353, "主题色 / Accent", ink);
        u32 colors[] = {0xe2e2e7, 0xc7c8cf, 0xb7bbbd, 0xd4d1c9};
        for (int i = 0; i < 4; i++) {
            circle(cx + 19 + i * 64, cy + 410, 18, colors[i]);
            if (accent_choice == i)
                circle(cx + 19 + i * 64, cy + 410, 6, 0xffffff);
        }
        text(cx, cy + 471, "外观设置在本次会话内生效。", muted);
    } else if (settings_tab == 2) {
        text(cx, cy, "STORAGE / SESSION", accent);
        large_text(cx, cy + 26, "存储空间", ink);
#ifdef MAYOS_LINUX
        text(cx, cy + 77, "RAM session / 本次桌面的临时文件", muted);
#else
        text(cx, cy + 77, "MayFS / 自研文件系统", muted);
#endif
        int used = 0, bytes = 0;
        for (int i = 0; i < FS_MAX; i++)
            if (files[i].used) {
                used++;
                bytes += files[i].size;
            }
        roundrect(cx, cy + 122, cw, 117, bg);
        text(cx + 18, cy + 138,
#ifdef MAYOS_LINUX
             "RAM / 不会挂载或修改 MayFS 数据盘",
#else
             disk_ready ? "disk1 / ATA 磁盘已连接" : "RAM / 磁盘未连接",
#endif
             ink);
        rect(cx + 18, cy + 177, cw - 36, 8, line);
        rect(cx + 18, cy + 177, (cw - 36) * used / FS_MAX + 1, 8, accent);
        numtext(cx + 18, cy + 201, used, ink);
        text(cx + 47, cy + 201, "/ 48 个目录项", muted);
        text(cx, cy + 271, "文件内容", muted);
        numtext(cx + 100, cy + 271, bytes, ink);
        text(cx + 172, cy + 271, "bytes", muted);
        text(cx, cy + 309, "单文件上限 4095 字节。", muted);
#ifdef MAYOS_LINUX
        text(cx, cy + 347, "Linux 桌面文件仅存在于此会话内存。", muted);
        text(cx, cy + 377, "退出或关机后，临时文件会清除。", muted);
#else
        text(cx, cy + 347, "连接有效 MayFS 数据盘时自动同步。", muted);
        text(cx, cy + 377, "原型存储格式；请使用专用虚拟数据盘。", muted);
        button(cx, cy + 436, 126, "立即同步", 1);
#endif
    } else {
        text(cx, cy, "OPEN SOURCE / CREDITS", accent);
        large_text(cx, cy + 26, "开源组件", ink);
        text(cx, cy + 77, "感谢以下项目。各组件保留各自许可证。", muted);
        const char *names[] = {"GNU GRUB", "Linux kernel", "BusyBox", "musl libc",
                                 "Fastfetch", "QEMU + OVMF", "Noto CJK / DejaVu"};
        const char *roles[] = {"启动管理 / GPL-3.0-or-later", "Linux 模式 / GPL-2.0-only",
                                 "Linux 用户空间 / GPL-2.0-only", "Linux C 运行库 / MIT",
                                 "系统信息工具 / MIT", "虚拟化与 UEFI 测试", "界面字体 / OFL-1.1"};
        for (int i = 0; i < 7; i++) {
            int yy = cy + 115 + i * 47;
            text(cx, yy, names[i], ink);
            text(cx + 184, yy, roles[i], muted);
            rect(cx, yy + 35, cw, 1, line);
        }
        text(cx, cy + 452, "QEMU: GPL-2.0 / OVMF: BSD-2-Clause-Patent", muted);
        text(cx, cy + 479, "完整许可与对应源码见发行包的 licenses 文档。", muted);
        text(cx, cy + 513, "MayOS Team 2023 - 2026.", ink);
    }
}
static int file_list[FS_MAX], file_count;
static void build_file_list(void) {
    file_count = 0;
    for (int i = 1; i < FS_MAX; i++)
        if (files[i].used && files[i].parent == (u32)folder)
            file_list[file_count++] = i;
}
static void files_draw(Window *w) {
    int x = w->x, y = w->y;
    rect(x + 1, y + 43, 146, w->h - 44, bg);
    text(x + 20, y + 65, "位置", muted);
    const char *nav[] = {
#ifdef MAYOS_LINUX
        "RAM / 根目录",
#else
        "disk1 / 根目录",
#endif
        "文档", "桌面", "回收站"};
    for (int i = 0; i < 4; i++) {
        if (folder == i)
            roundrect(x + 10, y + 103 + i * 40, 128, 33, selected_fill);
        text(x + 22, y + 109 + i * 40, nav[i], folder == i ? accent : ink);
    }
    text(x + 20, y + w->h - 55,
#ifdef MAYOS_LINUX
         "RAM SESSION",
#else
         "MayFS",
#endif
         muted);
    text(x + 20, y + w->h - 32, disk_ready ? "● 磁盘已挂载" : "● 内存模式", muted);
    int cx = x + 164;
    button(cx, y + 54, 42, "↑", 0);
    roundrect(cx + 52, y + 54, w->w - 233, 30, bg);
    text(cx + 64, y + 59,
#ifdef MAYOS_LINUX
         "RAM",
#else
         "disk1",
#endif
         muted);
    text(cx + 112, y + 59, "/", muted);
    if (folder)
        text(cx + 130, y + 59, files[folder].name, ink);
    button(cx, y + 99, 102, "新建文件", 0);
    button(cx + 114, y + 99, 118, "新建文件夹", 0);
    button(x + w->w - 120, y + 99, 44, "‹", 0);
    button(x + w->w - 66, y + 99, 44, "›", 0);
    text(cx, y + 147, "名称", muted);
    text(x + w->w - 190, y + 147, "类型", muted);
    text(x + w->w - 99, y + 147, "大小", muted);
    rect(cx, y + 176, w->w - 185, 1, line);
    build_file_list();
    int visible = (w->h - 225) / 44;
    if (file_scroll >= file_count)
        file_scroll = 0;
    for (int k = 0; k + file_scroll < file_count && k < visible; k++) {
        int i = file_list[k + file_scroll], yy = y + 184 + k * 44;
        if (selected == i)
            roundrect(cx - 4, yy, w->w - 178, 40, selected_fill);
        icon(cx + 7, yy + 8, files[i].dir ? 0 : 2, 24);
        text(cx + 45, yy + 10, files[i].name, ink);
        text(x + w->w - 190, yy + 10, files[i].dir ? "文件夹" : "文本文件", muted);
        if (!files[i].dir)
            numtext(x + w->w - 92, yy + 10, files[i].size, muted);
    }
    if (!file_count) {
        text(cx + 60, y + 230, "这里还很安静", muted);
        text(cx + 28, y + 263, "点击新建，写下第一个想法。", muted);
    }
    rect(cx, y + w->h - 34, w->w - 180, 1, line);
    numtext(cx, y + w->h - 27, file_count, muted);
    text(cx + 24, y + w->h - 27, "个项目 · 双击打开 / 右键管理", muted);
    if (file_count > visible) {
        text(cx + 288, y + w->h - 27, "起始项目", muted);
        numtext(cx + 360, y + w->h - 27, file_scroll + 1, muted);
    }
}
static void terminal_draw(Window *w) {
    int x = w->x + 1, y = w->y + 43;
    rect(x, y, w->w - 2, w->h - 44, dark ? 0x19161f : 0x2b2930);
    int rows = (w->h - 108) / 21;
    int end = out_count - scroll;
    if (end < 0)
        end = 0;
    int start = end - rows;
    if (start < 0)
        start = 0;
    for (int i = start; i < end; i++)
        text(x + 18, y + 12 + (i - start) * 21, output[i], 0xc4d5df);
    int py = w->y + w->h - 48;
    text(x + 18, py, "may@mayos", accent);
    int xx = text(x + 98, py, ":~ $ ", 0x90abc8);
    text(xx, py, command, 0xf1f6ff);
    if (ticks % 100 < 55)
        rect(xx + cmdlen * 8, py + 3, 7, 16, accent);
    rect(x + 1, w->y + w->h - 29, w->w - 4, 1, 0x21313f);
    text(x + 18, w->y + w->h - 24, "MayShell / " PLATFORM_SHORT, 0x678793);
    text(x + w->w - 220, w->y + w->h - 24, "↑ 历史   ↓ 清空   F1 终端", 0x678793);
}
static void editor_draw(Window *w) {
    int x = w->x, y = w->y;
    button(x + 17, y + 55, 90, "保存", 1);
    text(x + 126, y + 61, editor_file >= 0 ? files[editor_file].name : "没有打开文件", ink);
    if (editor_changed)
        text(x + w->w - 110, y + 61, "● 未保存", accent);
    rect(x + 17, y + 99, w->w - 34, 1, line);
    int xx = x + 22, yy = y + 116, col = 0;
    const char *s = editor_buf;
    while (*s && yy < y + w->h - 48) {
        const char *start = s;
        int c = utf(&s);
        if (c == '\n') {
            yy += 22;
            col = 0;
            continue;
        }
        int cw = c < 128 ? 1 : 2;
        if (col + cw > (w->w - 48) / 8) {
            yy += 22;
            col = 0;
        }
        char b[5];
        int len = s - start;
        memcpy(b, start, len);
        b[len] = 0;
        text(xx + col * 8, yy, b, ink);
        col += cw;
    }
    if (yy < y + w->h - 48 && ticks % 100 < 55)
        rect(xx + col * 8, yy + 2, 1, 18, accent);
    rect(x + 17, y + w->h - 36, w->w - 34, 1, line);
    text(x + 20, y + w->h - 29, "纯文本 · UTF-8 · 在文末输入", muted);
    numtext(x + w->w - 140, y + w->h - 29, strlen(editor_buf), muted);
    text(x + w->w - 89, y + w->h - 29, "/ 4095 B", muted);
}
/* Same browser chrome, navigation/history and text viewer on both kernels.
 * Linux and MayKernel retain their explicitly different HTTP transports. */
static char explorer_query[176], explorer_bookmarks[5][176];
static char explorer_history[12][176], explorer_response[8192], explorer_view[8192];
static int explorer_bookmark_count, explorer_history_count, explorer_history_index = -1;
static int explorer_loading, explorer_scroll, explorer_home = 1;
static int explorer_editing;

static void explorer_load(const char *candidate, int add_history) {
    char address[176];
    copy(address, candidate, sizeof(address));
    int length = strlen(address);
    while (length && address[length - 1] == ' ') address[--length] = 0;
    if (!length) {
        explorer_home = 1;
        explorer_loading = 0;
        explorer_response[0] = 0;
        explorer_scroll = 0;
        dirty = 1;
        return;
    }
    int http = address[0] == 'h' && address[1] == 't' &&
               address[2] == 't' && address[3] == 'p' &&
               address[4] == ':' && address[5] == '/' && address[6] == '/';
    int https = address[0] == 'h' && address[1] == 't' &&
                address[2] == 't' && address[3] == 'p' && address[4] == 's' &&
                address[5] == ':' && address[6] == '/' && address[7] == '/';
    if (!http && !https) {
        char prefix[176] = "http://";
        copy(prefix + 7, address, sizeof(prefix) - 7);
        copy(address, prefix, sizeof(address));
    }
    if (add_history &&
        (explorer_history_index < 0 ||
         strcmp(explorer_history[explorer_history_index], address))) {
        explorer_history_count = explorer_history_index + 1;
        if (explorer_history_count == 12) {
            for (int i = 1; i < 12; i++)
                copy(explorer_history[i - 1], explorer_history[i],
                     sizeof(explorer_history[i - 1]));
            explorer_history_count = 11;
        }
        copy(explorer_history[explorer_history_count], address,
             sizeof(explorer_history[0]));
        explorer_history_index = explorer_history_count++;
    }
    copy(explorer_query, address, sizeof(explorer_query));
    explorer_home = 0;
    explorer_editing = 0;
    explorer_scroll = 0;
    explorer_response[0] = 0;
    if (https) {
        copy(explorer_response,
             "HTTPS requires TLS, not yet available. No insecure downgrade was attempted.",
             sizeof(explorer_response));
        explorer_loading = 0;
        serial("Explorer: HTTPS unsupported\n");
    } else {
#ifdef MAYOS_LINUX
        explorer_loading = platform_web_start(explorer_query);
        platform_web_poll(explorer_response, sizeof(explorer_response));
#else
        explorer_loading = net_http_start(explorer_query);
        net_http_poll(explorer_response, sizeof(explorer_response));
#endif
        serial("Explorer: HTTP navigation\n");
    }
    dirty = 1;
}
static void explorer_submit(void) { explorer_load(explorer_query, 1); }
static void explorer_step(int delta) {
    int next = explorer_history_index + delta;
    if (next < 0 || next >= explorer_history_count) return;
    explorer_history_index = next;
    explorer_load(explorer_history[next], 0);
    serial(delta < 0 ? "Explorer: history back\n" : "Explorer: history forward\n");
}
static void explorer_reload(void) {
    if (explorer_home) return;
    explorer_load(explorer_query, 0);
    serial("Explorer: reload\n");
}
static void explorer_save_bookmark(void) {
    if (explorer_home || !explorer_query[0]) return;
    for (int i = 0; i < explorer_bookmark_count; i++)
        if (!strcmp(explorer_bookmarks[i], explorer_query)) {
            for (int j = i + 1; j < explorer_bookmark_count; j++)
                copy(explorer_bookmarks[j - 1], explorer_bookmarks[j],
                     sizeof(explorer_bookmarks[j - 1]));
            explorer_bookmark_count--;
            serial("Explorer: bookmark removed\n");
            notify("已移除收藏");
            return;
        }
    if (explorer_bookmark_count == 5) {
        for (int i = 1; i < 5; i++)
            copy(explorer_bookmarks[i - 1], explorer_bookmarks[i],
                 sizeof(explorer_bookmarks[i - 1]));
        explorer_bookmark_count = 4;
    }
    copy(explorer_bookmarks[explorer_bookmark_count++], explorer_query,
         sizeof(explorer_bookmarks[0]));
    serial("Explorer: bookmark added\n");
    notify("已收藏本页");
}
static int explorer_favorite(void) {
    for (int i = 0; i < explorer_bookmark_count; i++)
        if (!strcmp(explorer_bookmarks[i], explorer_query)) return 1;
    return 0;
}
static void explorer_draw(Window *w) {
    int x = w->x, y = w->y, width = w->w;
    /* Functional back/forward/reload/home controls, no inert decoration. */
    const char *nav[] = {"<", ">", "R", "H"};
    for (int i = 0; i < 4; i++) {
        int xx = x + 16 + i * 42;
        int disabled = i == 0 ? explorer_history_index <= 0 :
                       i == 1 ? explorer_history_index + 1 >= explorer_history_count :
                       i == 2 ? explorer_home : 0;
        roundrect(xx, y + 53, 36, 36,
                  inside(mx, my, xx, y + 53, 36, 36) && !disabled ?
                  selected_fill : surface_high);
        text(xx + 11, y + 60, nav[i], disabled ? muted : ink);
    }
    roundrect(x + 192, y + 53, width - 308, 36, surface_high);
    symbol(x + 201, y + 61, 19, 18, muted);
    char display[62];
    copy(display, explorer_query[0] ? explorer_query : "输入 http:// 地址",
         sizeof(display));
    text(x + 227, y + 60, display, explorer_query[0] ? ink : muted);
    if (explorer_editing) rect(x + 227 + (int)strlen(display) * 8,
                                y + 61, 2, 21, accent);
    roundrect(x + width - 105, y + 53, 88, 36,
              inside(mx, my, x + width - 105, y + 53, 88, 36)
              ? selected_fill : surface_high);
    text(x + width - 90, y + 60, explorer_favorite() ? "已收藏" : "收藏", ink);
    text(x + 20, y + 104, "收藏夹", muted);
    for (int i = 0; i < explorer_bookmark_count; i++) {
        int xx = x + 88 + i * 132;
        char label[16];
        copy(label, explorer_bookmarks[i] + 7, sizeof(label));
        roundrect(xx, y + 100, 126, 30,
                  inside(mx, my, xx, y + 100, 126, 30) ?
                  selected_fill : surface_high);
        text(xx + 7, y + 104, label, ink);
    }
    rect(x + 16, y + 140, width - 32, 1, line);
    if (explorer_home) {
        symbol(x + 34, y + 169, 4, 46, accent);
        large_text(x + 102, y + 174, "Explorer", ink);
        text(x + 38, y + 250, "输入网址并按 Enter 以访问 HTTP 文本页面", ink);
        text(x + 38, y + 285, "支持返回、前进、刷新、收藏、文本阅读和滚动", muted);
        text(x + 38, y + 318, "HTTPS、JavaScript、图片和 CSS 网页排版尚不支持", muted);
        text(x + 38, y + 355, "在 QEMU 中可访问 http://10.0.2.2:端口/", muted);
    } else {
        char state[68];
        copy(state, explorer_loading ? "加载中..." : "文本阅读模式", sizeof(state));
        text(x + 22, y + 153, state, accent);
        text(x + width - 300, y + 153, "不执行网页脚本", muted);
        browse_preview(explorer_response, explorer_view, sizeof(explorer_view));
        int max_cols = (width - 66) / 8;
        if (max_cols > 105) max_cols = 105;
        const char *ptr = explorer_view;
        int index = 0, painted = 0;
        int max_rows = (w->h - 248) / 19;
        while (*ptr && painted < max_rows) {
            char linebuf[108];
            int count = 0;
            while (*ptr && *ptr != '\n' && count < max_cols)
                linebuf[count++] = *ptr++;
            if (*ptr == '\n') ptr++;
            linebuf[count] = 0;
            if (index++ >= explorer_scroll) {
                text(x + 27, y + 184 + painted * 19, linebuf, ink);
                painted++;
            }
        }
        if (!explorer_view[0] && !explorer_loading)
            text(x + 26, y + 194, "没有可显示的文本内容", muted);
    }
    rect(x + 16, y + w->h - 62, width - 32, 1, line);
    roundrect(x + 18, y + w->h - 51, 95, 34, surface_high);
    text(x + 29, y + w->h - 46, "上一页", ink);
    roundrect(x + width - 113, y + w->h - 51, 95, 34, surface_high);
    text(x + width - 102, y + w->h - 46, "下一页", ink);
    text(x + width / 2 - 80, y + w->h - 45,
         explorer_loading ? "HTTP 请求进行中" : "文本预览 · HTTP", muted);
}
static void taskbar(void) {
    /* Centered Material 3 dock with state-layer hover and active indicator. */
    int left = 210, top = H - 84;
    roundrect(left + 3, top + 5, 604, 76,
              dark ? 0x0f1012 : 0xc7c9cd);
    roundrect(left, top, 604, 76, dark ? 0x242528 : 0xf4f4f6);
    rect(left + 25, top + 1, 553, 1, dark ? 0x45464b : 0xffffff);
    for (int i = 0; i < 6; i++) {
        int x = left + 16 + i * 98;
        int id = i - 1;
        int active = i == 0 ? start_open :
                     (id >= 0 && windows[id].open && !windows[id].min);
        int hover = inside(mx, my, x - 7, top + 4, 61, 65);
        if (active || hover)
            roundrect(x - 5, top + 3, 54, 58,
                      active ? selected_fill : surface_high);
        int kind = i == 0 ? 6 : id == 3 ? 5 : id;
        icon(x, top + 8 - (hover ? 2 : 0), kind, 44);
        if (active)
            roundrect(x + 16, top + 66, 13, 3, accent);
    }
}
static int contains(const char *a, const char *b) {
    if (!*b)
        return 1;
    for (; *a; a++) {
        int i = 0;
        while (b[i] && a[i] == b[i])
            i++;
        if (!b[i])
            return 1;
    }
    return 0;
}
static int search_match(int id) {
    const char *names[] = {"files 文件 文档 folder", "settings 设置 关于",
                           "terminal 终端 shell", "editor 文本 编辑",
                           "explorer 浏览器 network", "trash 回收站 删除"};
    return id >= 0 && id < 6 && contains(names[id], search);
}
static void start_draw(void) {
    int x = 180, y = 130, width = 664, height = 500;
    roundrect(x + 6, y + 8, width, height, dark ? 0x141218 : 0xd7d2da);
    roundrect(x, y, width, height, panel);
    text(x + 30, y + 24, "应用程序", ink);
    text(x + 530, y + 24, "全部应用", muted);
    roundrect(x + 28, y + 68, width - 56, 44, surface_high);
    symbol(x + 41, y + 81, 4, 20, muted);
    text(x + 77, y + 78, search[0] ? search : "搜索应用 / files, settings, terminal",
         search[0] ? ink : muted);
    const char *names[] = {"文件管理器", "系统设置", "终端", "文本编辑器", "Explorer", "回收站"};
    const char *details[] = {"本机文件", "外观与系统", "MayShell", "编辑文本", "网络预览", "已删除项目"};
    const int kinds[] = {0, 1, 2, 5, 4, 3};
    for (int i = 0; i < 6; i++) {
        if (!search_match(i)) continue;
        int xx = x + 31 + (i % 3) * 210;
        int yy = y + 147 + (i / 3) * 151;
        roundrect(xx, yy, 186, 123, surface_high);
        icon(xx + 14, yy + 15, kinds[i], 48);
        text(xx + 14, yy + 73, names[i], ink);
        text(xx + 14, yy + 98, details[i], muted);
    }
    text(x + 32, y + height - 38, "MayOS", muted);
    text(x + width - 184, y + height - 38, "重启", ink);
    text(x + width - 88, y + height - 38, "关机", ink);
}
static void context_draw(void) {
    roundrect(ctxx + 3, ctxy + 3, 184, 320, dark ? 0x141218 : 0xd8d1df);
    roundrect(ctxx, ctxy, 184, 320, panel);
    const char *items[] = {"打开", "新建文件", "新建文件夹",        "重命名", "复制",
                           "粘贴", "剪切",     "移到回收站 / 删除", "属性"};
    for (int i = 0; i < 9; i++)
        text(ctxx + 15, ctxy + 9 + i * 34, items[i], i == 7 ? 0xda7373 : ink);
}
static void modal_draw(void) {
    int x = 308, y = 262;
    roundrect(x + 5, y + 6, 408, 202, dark ? 0x141218 : 0xd8d1df);
    roundrect(x, y, 408, 202, panel);
    const char *title = modal == 1   ? "新建文本文件"
                        : modal == 2 ? "新建文件夹"
                        : modal == 3 ? "重命名"
                        : modal == 4 ? "重命名设备"
                        : modal == 6 ? "Ask ChatGPT / 帮助"
                                     : "文件属性";
    text(x + 23, y + 19, title, ink);
    if (modal == 6) {
        text(x + 23, y + 60, "MayOS " MAYOS_RELEASE " / 实验版本", ink);
        text(x + 23, y + 90, "此桌面尚未集成在线 ChatGPT 服务。", muted);
        text(x + 23, y + 119, "可在终端输入 help 查看本地帮助。", muted);
    } else if (modal == 5) {
        if (modal_target >= 0) {
            File *f = &files[modal_target];
            text(x + 23, y + 58, f->name, ink);
            text(x + 23, y + 87, f->dir ? "类型：文件夹" : "类型：文本文件 / UTF-8", muted);
            numtext(x + 23, y + 114, f->size, muted);
            text(x + 83, y + 114, "字节 · MayFS", muted);
        }
    } else {
        text(x + 23, y + 52, "名称（ASCII，最多 47 字节）", muted);
        roundrect(x + 22, y + 83, 364, 36, bg);
        text(x + 33, y + 91, modal_text, ink);
        rect(x + 34 + strlen(modal_text) * 8, y + 91, 1, 20, accent);
    }
    button(x + 204, y + 151, 80, "取消", 0);
    button(x + 296, y + 151, 90, "确定", 1);
}
/* Kept outside the normal window list: hidden until explicitly discovered. */
static void easter_draw(void) {
    roundrect(288, 72, 460, 626, dark ? 0x101a29 : 0xb9c8d7);
    roundrect(282, 66, 460, 626, panel);
    text(306, 85, "喵！你发现了一位小住客", ink);
    text(710, 83, "×", muted);
    int px = (W - CAT_W) / 2, py = 124;
    for (int y = 0; y < CAT_H; y++)
        for (int x = 0; x < CAT_W; x++) {
            u16 c = cat_rgb565[y * CAT_W + x];
            u32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            pixels[(py + y) * W + px + x] =
                ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        }
    text(306, 623, "喵，原来你在这里。", ink);
    text(306, 653, "按 Esc 返回", muted);
    button(610, 646, 108, "返回桌面", 1);
}
static void quick_draw(void) {
    int x = 689, y = 52;
    roundrect(x + 4, y + 5, 319, 336, dark ? 0x141218 : 0xd7d2da);
    roundrect(x, y, 319, 336, panel);
    text(x + 24, y + 21, "快捷设置", ink);
    text(x + 24, y + 70, "外观", muted);
    roundrect(x + 18, y + 101, 283, 44, surface_high);
    symbol(x + 31, y + 109, 9, 26, ink);
    text(x + 72, y + 112, dark ? "深色模式" : "浅色模式", ink);
    text(x + 244, y + 112, "切换", accent);
    text(x + 24, y + 168, "显示亮度", ink);
    roundrect(x + 18, y + 195, 283, 48, surface_high);
    text(x + 38, y + 202, "-", ink);
    rect(x + 80, y + 218, 167, 5, line);
    rect(x + 80, y + 218, 167 * brightness_level / 100, 5, accent);
    text(x + 267, y + 202, "+", ink);
    text(x + 24, y + 267, "网络连接", ink);
#ifdef MAYOS_LINUX
    char report[240];
    int online = platform_network_status(report, sizeof(report));
#else
    int online = net_online();
#endif
    symbol(x + 27, y + 298, 8, 24, online ? accent : muted);
    text(x + 69, y + 297, online ? "网络已连接" : "未获取 IP 地址", ink);
    text(x + 248, y + 297, "刷新", accent);
}
static void notifications_draw(void) {
    int x = 683, y = 52;
    roundrect(x + 4, y + 5, 324, 314, dark ? 0x141218 : 0xd7d2da);
    roundrect(x, y, 324, 314, panel);
    text(x + 23, y + 19, "通知中心", ink);
    text(x + 250, y + 19, "清空", accent);
    if (!notification_count) {
        symbol(x + 143, y + 90, 7, 38, muted);
        text(x + 88, y + 167, "暂无新通知", muted);
    } else {
        for (int i = 0; i < notification_count && i < 3; i++) {
            int yy = y + 63 + i * 75;
            roundrect(x + 15, yy, 293, 66, surface_high);
            text(x + 27, yy + 12, "MayOS", accent);
            text(x + 27, yy + 38, notification_log[i], ink);
        }
    }
}
static void overview_draw(void) {
    int x = 158, y = 112;
    roundrect(x + 6, y + 8, 708, 520, dark ? 0x141218 : 0xd7d2da);
    roundrect(x, y, 708, 520, panel);
    text(x + 26, y + 23, "窗口总览", ink);
    text(x + 530, y + 23, "Esc 返回", muted);
    int shown = 0;
    for (int i = 0; i < WINDOW_COUNT; i++) {
        if (!windows[i].open) continue;
        int xx = x + 26 + (shown % 2) * 340;
        int yy = y + 78 + (shown / 2) * 144;
        roundrect(xx, yy, 317, 126, i == focus ? selected_fill : surface_high);
        symbol(xx + 20, yy + 18, i == 3 ? 5 : i, 32, ink);
        text(xx + 65, yy + 23, windows[i].title, ink);
        text(xx + 65, yy + 58, windows[i].min ? "已最小化" : "正在运行", muted);
        shown++;
    }
    if (!shown) text(x + 30, y + 110, "没有打开的窗口", muted);
}
static void desktop_context_draw(void) {
    roundrect(desktop_x + 3, desktop_y + 4, 242, 179, dark ? 0x141218 : 0xd7d2da);
    roundrect(desktop_x, desktop_y, 242, 179, panel);
    const char *choices[] = {"新建桌面文件", "新建文件夹", "切换壁纸", "显示系统设置"};
    for (int i = 0; i < 4; i++)
        text(desktop_x + 18, desktop_y + 9 + i * 41, choices[i], ink);
}
static void clock_draw(void) {
    int x = 728, y = 52;
    roundrect(x + 4, y + 5, 280, 170, dark ? 0x101114 : 0xc7c9cd);
    roundrect(x, y, 280, 170, panel);
    text(x + 23, y + 18, "日期与时间", muted);
    char current[8];
    platform_clock(current);
    large_text(x + 25, y + 51, current, ink);
    text(x + 25, y + 130, "系统时钟", muted);
}
static void power_draw(void) {
    int x = 767, y = 52;
    roundrect(x + 4, y + 5, 241, 160, dark ? 0x101114 : 0xc7c9cd);
    roundrect(x, y, 241, 160, panel);
    text(x + 20, y + 16, "电源选项", ink);
    roundrect(x + 15, y + 51, 211, 42, surface_high);
    symbol(x + 23, y + 59, 12, 24, ink);
    text(x + 64, y + 59, "关机", ink);
    roundrect(x + 15, y + 103, 211, 42, surface_high);
    symbol(x + 24, y + 111, 11, 24, ink);
    text(x + 64, y + 111, "重启", ink);
}
static void cursor(void) {
    /* Classic Windows arrow: white fill, high-contrast dark border and shadow.
       Hotspot is the upper-left black tip. All mouse input remains unchanged. */
    static const char *const pixels_shape[] = {
        "#               ",
        "##              ",
        "#+#             ",
        "#++#            ",
        "#+++#           ",
        "#++++#          ",
        "#+++++#         ",
        "#++++++#        ",
        "#+++++++#       ",
        "#++++++++#      ",
        "#+++++++++#     ",
        "#++++++++++#    ",
        "#+++++++++++#   ",
        "#++++++++++++#  ",
        "#+++++++++++++# ",
        "#+++++########  ",
        "#+++#+#         ",
        "#++# #+#        ",
        "#+#  #++#       ",
        "##   #++#       ",
        "#     #++#      ",
        "      #++#      ",
        "      ####      "
    };
    for (int y = 0; y < 23; y++)
        for (int x = 0; x < 16; x++)
            if (pixels_shape[y][x] != ' ')
                rect(mx + x + 2, my + y + 2, 1, 1, 0x54565c);
    for (int y = 0; y < 23; y++)
        for (int x = 0; x < 16; x++) {
            char pixel = pixels_shape[y][x];
            if (pixel == '#')
                rect(mx + x, my + y, 1, 1, 0x111215);
            else if (pixel == '+')
                rect(mx + x, my + y, 1, 1, 0xffffff);
        }
}
static void render(void) {
    palette();
    desktop();
    for (int k = 0; k < WINDOW_COUNT; k++) {
        int i = order[k];
        Window *w = &windows[i];
        if (!w->open || w->min)
            continue;
        frame(i);
        if (i == 0)
            files_draw(w);
        else if (i == 1)
            settings(w);
        else if (i == 2)
            terminal_draw(w);
        else if (i == 3)
            editor_draw(w);
        else
            explorer_draw(w);
    }
    taskbar();
    if (start_open) start_draw();
    if (overview_open) overview_draw();
    if (quick_open) quick_draw();
    if (notifications_open) notifications_draw();
    if (clock_open) clock_draw();
    if (power_open) power_draw();
    if (desktop_context_open) desktop_context_draw();
    if (context_open)
        context_draw();
    if (notice_until > ticks) {
        roundrect(300, H - 104, 520, 40, surface_high);
        text(316, H - 95, notice, ink);
    }
    if (modal)
        modal_draw();
    if (egg_open)
        easter_draw();
    cursor();
    if (brightness_level != 100) {
        for (int index = 0; index < W * H; index++) {
            u32 color = pixels[index];
            u32 r = ((color >> 16) & 255u) * (u32)brightness_level / 100u;
            u32 g = ((color >> 8) & 255u) * (u32)brightness_level / 100u;
            u32 b = (color & 255u) * (u32)brightness_level / 100u;
            pixels[index] = r << 16 | g << 8 | b;
        }
    }
    present(pixels);
    dirty = 0;
}
static void toggle_max(int id) {
    Window *w = &windows[id];
    if (!w->max) {
        w->ox = w->x;
        w->oy = w->y;
        w->ow = w->w;
        w->oh = w->h;
        w->x = 112;
        w->y = 56;
        w->w = W - 128;
        w->h = H - 118;
        w->max = 1;
    } else {
        w->x = w->ox;
        w->y = w->oy;
        w->w = w->ow;
        w->h = w->oh;
        w->max = 0;
    }
}
static void paste_file(void) {
    if (clip < 4 || !files[clip].used) {
        notify("请先选择文件并复制");
        return;
    }
    if (cut) {
        if (!fs_move(clip, folder)) {
            notify("无法移动到此目录");
            return;
        }
        cut = 0;
        clip = -1;
    } else {
        if (files[clip].dir) {
            notify("此版本仅支持复制文本文件");
            return;
        }
        char name[48];
        copy(name, files[clip].name, 38);
        copy(name + strlen(name), "-copy", 48 - strlen(name));
        int i = fs_create(folder, name, 0);
        if (i < 0) {
            notify("复制失败：同名文件或空间不足");
            return;
        }
        memcpy(files[i].data, files[clip].data, FILE_CAP);
        files[i].size = files[clip].size;
        selected = i;
    }
    sync_notice();
    notify("操作完成");
}
static void trash_selected(void) {
    if (selected < 4) {
        notify("请先选择普通文件或文件夹");
        return;
    }
    if (editor_file == selected && editor_changed) {
        notify("请先保存正在编辑的文件");
        return;
    }
    if (folder == 3) {
        if (editor_file >= 0) {
            int p = editor_file;
            while (p && p != selected)
                p = files[p].parent;
            if (p == selected) {
                if (editor_changed) {
                    notify("请先保存子目录中正在编辑的文件");
                    return;
                }
                editor_file = -1;
                editor_buf[0] = 0;
                windows[3].open = 0;
            }
        }
        if (!fs_remove(selected)) {
            notify("删除失败");
            return;
        }
    } else if (!fs_move(selected, 3)) {
        notify("移入失败：回收站已有同名文件");
        return;
    }
    selected = -1;
    sync_notice();
}
static void context_action(int i) {
    context_open = 0;
    if (i == 0)
        open_file(selected);
    else if (i == 1)
        begin_modal(1, -1);
    else if (i == 2)
        begin_modal(2, -1);
    else if (i == 3) {
        if (selected >= 0)
            begin_modal(3, selected);
        else
            notify("请先选择文件");
    } else if (i == 4 || i == 6) {
        if (selected >= 4) {
            clip = selected;
            cut = i == 6;
            notify(cut ? "已剪切；在目标目录粘贴" : "已复制；在目标目录粘贴");
        } else
            notify("请先选择普通文件");
    } else if (i == 5)
        paste_file();
    else if (i == 7)
        trash_selected();
    else if (i == 8 && selected >= 0)
        begin_modal(5, selected);
}

static void left_click(void) {
    dirty = 1;
    if (egg_open) {
        if (inside(mx, my, 696, 76, 38, 34) || inside(mx, my, 610, 646, 108, 30))
            egg_open = 0;
        return;
    }
    Window *about = &windows[1];
    if (!(about->open && !about->min && focus == 1 && settings_tab == 0 &&
          inside(mx, my, about->x + 220, about->y + 190, 44, 44)))
        egg_clicks = 0;
    if (modal) {
        if (inside(mx, my, 604, 413, 90, 30))
            accept_modal();
        else if (inside(mx, my, 512, 413, 80, 30))
            modal = 0;
        return;
    }
    if (context_open) {
        if (inside(mx, my, ctxx, ctxy, 184, 320)) {
            context_action((my - ctxy - 4) / 34);
            return;
        }
        context_open = 0;
    }
    if (desktop_context_open) {
        desktop_context_open = 0;
        if (inside(mx, my, desktop_x, desktop_y, 242, 179)) {
            int index = (my - desktop_y) / 41;
            if (index == 0 || index == 1) {
                folder = 2;
                begin_modal(index ? 2 : 1, -1);
            } else if (index == 2)
                wallpaper_mode = !wallpaper_mode;
            else if (index == 3)
                raise_window(1);
        }
        return;
    }
    if (clock_open) {
        clock_open = 0;
        return;
    }
    if (power_open) {
        power_open = 0;
        if (inside(mx, my, 782, 103, 211, 42)) {
            fs_sync();
            poweroff();
        } else if (inside(mx, my, 782, 155, 211, 42)) {
            fs_sync();
            reboot();
        }
        return;
    }
    if (quick_open) {
        quick_open = 0;
        if (inside(mx, my, 689, 52, 319, 336)) {
            if (inside(mx, my, 707, 153, 283, 44)) dark = !dark;
            else if (inside(mx, my, 707, 247, 68, 48)) {
                brightness_level -= 10;
                if (brightness_level < 30) brightness_level = 30;
            } else if (inside(mx, my, 936, 247, 72, 48)) {
                brightness_level += 10;
                if (brightness_level > 100) brightness_level = 100;
            } else if (inside(mx, my, 919, 340, 90, 47)) {
#ifndef MAYOS_LINUX
                net_command("renew");
#else
                char status[200];
                platform_network_status(status, sizeof(status));
#endif
                notify("已重新检查网络状态");
            } else quick_open = 1;
        }
        return;
    }
    if (notifications_open) {
        notifications_open = 0;
        if (inside(mx, my, 919, 52, 88, 63)) {
            notification_count = 0;
            notice[0] = 0;
        }
        return;
    }
    if (overview_open) {
        overview_open = 0;
        if (inside(mx, my, 158, 112, 708, 520)) {
            int current = 0;
            for (int i = 0; i < WINDOW_COUNT; i++) {
                if (!windows[i].open) continue;
                int x = 184 + (current % 2) * 340;
                int y = 190 + (current / 2) * 144;
                if (inside(mx, my, x, y, 317, 126)) {
                    raise_window(i);
                    return;
                }
                current++;
            }
        }
        return;
    }
    if (my < 42) {
        if (mx < 110 || (mx >= 264 && mx < 346)) {
            start_open = !start_open;
            search[0] = 0;
        } else if (mx >= 110 && mx < 185) {
            raise_window(0);
        } else if (mx >= 185 && mx < 262) {
            overview_open = !overview_open;
        } else if (mx >= 346 && mx < 425) {
            raise_window(2);
        } else if (mx >= 706 && mx < 756) {
            clock_open = !clock_open;
            quick_open = notifications_open = power_open = 0;
        } else if (mx >= 756 && mx < 856) {
            quick_open = !quick_open;
            clock_open = notifications_open = power_open = 0;
        } else if (mx >= 856 && mx < 906) {
            notifications_open = !notifications_open;
            clock_open = quick_open = power_open = 0;
        } else if (mx >= 906 && mx < 956) {
            power_open = !power_open;
            clock_open = quick_open = notifications_open = 0;
        } else if (mx >= 956) {
            quick_open = !quick_open;
            clock_open = notifications_open = power_open = 0;
        }
        return;
    }
    if (my >= H - 84 && inside(mx, my, 210, H - 84, 604, 76)) {
        int app = (mx - 226) / 98;
        if (mx < 226 || app < 0 || app >= 6) return;
        if (app == 0) {
            start_open = !start_open;
            search[0] = 0;
        } else {
            int id = app - 1;
            if (windows[id].open && focus == id && !windows[id].min)
                windows[id].min = 1;
            else
                raise_window(id);
        }
        return;
    }
    if (start_open) {
        if (inside(mx, my, 180, 130, 664, 500)) {
            for (int i = 0; i < 6; i++) {
                int xx = 211 + (i % 3) * 210;
                int yy = 277 + (i / 3) * 151;
                if (!search_match(i) || !inside(mx, my, xx, yy, 186, 123))
                    continue;
                if (i == 5) {
                    folder = 3;
                    selected = -1;
                    raise_window(0);
                } else if (i == 3 && editor_file < 0) {
                    folder = 1;
                    raise_window(0);
                    begin_modal(1, -1);
                } else raise_window(i);
                return;
            }
            if (my >= 582 && mx >= 646 && mx < 844) {
                fs_sync();
                if (mx > 752) poweroff(); else reboot();
            }
            return;
        }
        start_open = 0;
    }
    for (int k = WINDOW_COUNT - 1; k >= 0; k--) {
        int id = order[k];
        Window *w = &windows[id];
        if (!w->open || w->min || !inside(mx, my, w->x, w->y, w->w, w->h))
            continue;
        raise_window(id);
        int rx = mx - w->x, ry = my - w->y;
        if (!w->max && rx >= w->w - 22 && ry >= w->h - 22) {
            resizing = id;
            drag = -1;
            return;
        }
        if (ry < 42) {
            if (rx > w->w - 46) {
                if (id == 3 && editor_changed) {
                    notify("请先保存修改，再关闭编辑器");
                    return;
                }
                w->open = 0;
                return;
            }
            if (rx > w->w - 82) {
                toggle_max(id);
                return;
            }
            if (rx > w->w - 119) {
                w->min = 1;
                return;
            }
            if (!w->max) {
                drag = id;
                dragx = rx;
                dragy = ry;
            }
            return;
        }
        if (id == 4) {
            if (inside(rx, ry, 16, 53, 36, 36)) explorer_step(-1);
            else if (inside(rx, ry, 58, 53, 36, 36)) explorer_step(1);
            else if (inside(rx, ry, 100, 53, 36, 36)) explorer_reload();
            else if (inside(rx, ry, 142, 53, 36, 36)) {
                explorer_home = 1;
                explorer_scroll = 0;
                explorer_loading = 0;
                explorer_response[0] = 0;
                explorer_editing = 0;
                serial("Explorer: home\n");
            } else if (inside(rx, ry, 192, 53, w->w - 308, 36)) {
                /* Selecting the address bar replaces the old URL on typing. */
                explorer_query[0] = 0;
                explorer_editing = 1;
            } else if (inside(rx, ry, w->w - 105, 53, 88, 36)) {
                explorer_save_bookmark();
            } else if (ry >= 100 && ry < 130 && rx >= 88 &&
                       rx < 88 + explorer_bookmark_count * 132) {
                int item = (rx - 88) / 132;
                if ((rx - 88) % 132 < 126)
                    explorer_load(explorer_bookmarks[item], 1);
            } else if (inside(rx, ry, 18, w->h - 51, 95, 34)) {
                if (explorer_scroll >= 8) explorer_scroll -= 8;
                else explorer_scroll = 0;
                serial("Explorer: page up\n");
            } else if (inside(rx, ry, w->w - 113, w->h - 51, 95, 34)) {
                if (explorer_scroll < 96) explorer_scroll += 8;
                serial("Explorer: page down\n");
            }
            return;
        }
        if (id == 1) {
            if (settings_tab == 0 && inside(rx, ry, 220, 190, 44, 44)) {
                if (ticks - egg_last_click > 150)
                    egg_clicks = 0;
                egg_last_click = ticks;
                if (++egg_clicks >= 5)
                    open_easter();
                return;
            }
            if (rx < 177) {
                int tab = (ry - 106) / 46;
                if (ry >= w->h - 110 && ry < w->h - 75)
                    begin_modal(6, -1);
                else if (ry >= 106 && tab < 4 && (ry - 106) % 46 < 36)
                    settings_tab = tab;
            } else if (settings_tab == 0 && inside(rx, ry, 204, 514, 126, 30))
                begin_modal(4, -1);
            else if (settings_tab == 0 && inside(rx, ry, 344, 514, 194, 30))
                settings_tab = 3;
            else if (settings_tab == 1) {
                if (ry >= 220 && ry <= 382) {
                    if (rx >= 204 && rx < 420)
                        dark = 0;
                    else if (rx >= 440 && rx < 656)
                        dark = 1;
                }
                if (ry >= 457 && ry <= 493 && rx >= 204) {
                    int i = (rx - 204) / 64;
                    if (i >= 0 && i < 4)
                        accent_choice = i;
                }
            } else if (settings_tab == 2 && inside(rx, ry, 204, 501, 126, 30))
                notify(fs_sync() ? "已同步到磁盘" : "磁盘不可写");
        } else if (id == 0) {
            if (rx < 146) {
                int nav = (ry - 103) / 40;
                if (ry >= 103 && nav < 4) {
                    folder = nav;
                    file_scroll = 0;
                    selected = -1;
                }
            } else if (ry >= 54 && ry < 84 && rx < 206) {
                folder = files[folder].parent;
                file_scroll = 0;
                selected = -1;
            } else if (ry >= 99 && ry < 129) {
                if (rx >= w->w - 120 && rx < w->w - 76) {
                    file_scroll -= 5;
                    if (file_scroll < 0)
                        file_scroll = 0;
                } else if (rx >= w->w - 66) {
                    build_file_list();
                    if (file_scroll + 5 < file_count)
                        file_scroll += 5;
                } else if (rx >= 164 && rx < 266)
                    begin_modal(1, -1);
                else if (rx >= 278 && rx < 396)
                    begin_modal(2, -1);
            } else if (ry >= 184 && ry < w->h - 40) {
                int n = (ry - 184) / 44 + file_scroll;
                build_file_list();
                if (n < file_count) {
                    int i = file_list[n];
                    selected = i;
                    if (last_clicked == i && ticks - last_click_tick < 45) {
                        open_file(i);
                        last_clicked = -1;
                    } else {
                        last_clicked = i;
                        last_click_tick = ticks;
                    }
                } else
                    selected = -1;
            }
        } else if (id == 3 && inside(rx, ry, 17, 55, 90, 30)) {
            save_editor();
        }
        return;
    }
    int item = desktop_icon_at(mx, my);
    if (item >= 0) {
        if (last_clicked == item && ticks - last_click_tick < 45) {
            open_file(item);
            last_clicked = -1;
        } else {
            selected = item;
            last_clicked = item;
            last_click_tick = ticks;
        }
    }
}
static void right_click(void) {
    if (egg_open)
        return;
    if (modal)
        return;
    Window *w = &windows[0];
    if (w->open && !w->min && focus == 0 &&
        inside(mx, my, w->x + 146, w->y + 135, w->w - 146, w->h - 135)) {
        int n = (my - w->y - 184) / 44 + file_scroll;
        build_file_list();
        if (my >= w->y + 184 && n >= 0 && n < file_count)
            selected = file_list[n];
        ctxx = mx;
        ctxy = my;
        if (ctxx > W - 190)
            ctxx = W - 190;
        if (ctxy > H - 380)
            ctxy = H - 380;
        context_open = 1;
        dirty = 1;
        return;
    }
    for (int i = 0; i < WINDOW_COUNT; i++)
        if (windows[i].open && !windows[i].min &&
            inside(mx, my, windows[i].x, windows[i].y, windows[i].w, windows[i].h))
            return;
    int item = desktop_icon_at(mx, my);
    if (item >= 0) {
        selected = item;
        ctxx = mx > W - 190 ? W - 190 : mx;
        ctxy = my > H - 380 ? H - 380 : my;
        context_open = 1;
    } else {
        desktop_x = mx > W - 250 ? W - 250 : mx;
        desktop_y = my > H - 190 ? H - 190 : my;
        desktop_context_open = 1;
    }
    dirty = 1;
}
static void key_event(int k) {
    dirty = 1;
    if (egg_open) {
        if (k == 27 || k == '\n')
            egg_open = 0;
        return;
    }
    egg_clicks = 0;
    if (k == 0x105) {
        raise_window(2);
        return;
    }
    if (k == 0x104) {
        start_open = !start_open;
        search[0] = 0;
        return;
    }
    if (modal) {
        int n = strlen(modal_text);
        if (k == 27)
            modal = 0;
        else if (k == '\n')
            accept_modal();
        else if (k == '\b' && n)
            modal_text[n - 1] = 0;
        else if (k >= 32 && k < 127 && n < 47) {
            modal_text[n] = k;
            modal_text[n + 1] = 0;
        }
        return;
    }
    if (k == 27) {
        start_open = context_open = quick_open = notifications_open = overview_open = desktop_context_open = clock_open = power_open = 0;
        return;
    }
    if (k == 0x107) {
        overview_open = !overview_open;
        return;
    }
    if (start_open) {
        int n = strlen(search);
        if (k == '\b' && n)
            search[n - 1] = 0;
        else if (k >= 32 && k < 127 && n < 36) {
            search[n] = k;
            search[n + 1] = 0;
        } else if (k == '\n') {
            for (int i = 0; i < 6; i++)
                if (search_match(i)) {
                    if (i == 5) {
                        folder = 3;
                        raise_window(0);
                    } else if (i == 3 && editor_file < 0) {
                        folder = 1;
                        raise_window(0);
                        begin_modal(1, -1);
                    } else
                        raise_window(i);
                    break;
                }
        }
        return;
    }
    if (focus == 4 && windows[4].open && !windows[4].min) {
        int n = strlen(explorer_query);
        if (k == '\n') explorer_submit();
        else if (k == 0x101) {
            if (explorer_scroll) explorer_scroll--;
        } else if (k == 0x102) {
            if (explorer_scroll < 96) explorer_scroll++;
        } else if (k == '\b' && n) {
            explorer_query[n - 1] = 0;
            explorer_editing = 1;
        } else if (k >= 32 && k < 127 && n < (int)sizeof(explorer_query) - 1) {
            explorer_query[n] = k;
            explorer_query[n + 1] = 0;
            explorer_editing = 1;
        }
        return;
    }
    if (focus == 2 && windows[2].open && !windows[2].min) {
        if (k == '\n') {
            shell();
            cmdlen = 0;
            command[0] = 0;
        } else if (k == '\b' && cmdlen)
            command[--cmdlen] = 0;
        else if (k == 0x101) {
            copy(command, history, 160);
            cmdlen = strlen(command);
        } else if (k == 0x102) {
            command[0] = 0;
            cmdlen = 0;
        } else if (k >= 32 && k < 127 && cmdlen < 72) {
            command[cmdlen++] = k;
            command[cmdlen] = 0;
        }
    } else if (focus == 3 && windows[3].open && !windows[3].min && editor_file >= 0) {
        int n = strlen(editor_buf);
        if (k == 0x106) {
            save_editor();
            return;
        }
        if (k == '\b' && n) {
            int i = n - 1;
            while (i > 0 && ((u8)editor_buf[i] & 0xc0) == 0x80)
                i--;
            editor_buf[i] = 0;
            editor_changed = 1;
        } else if ((k == '\n' || k == '\t' || (k >= 32 && k < 127)) && n < FILE_CAP - 1) {
            editor_buf[n] = k;
            editor_buf[n + 1] = 0;
            editor_changed = 1;
        }
    } else if (focus == 0 && windows[0].open && !windows[0].min) {
        if (k == '\n')
            open_file(selected);
        else if (k == 0x103)
            trash_selected();
        else if (k == '\b') {
            folder = files[folder].parent;
            file_scroll = 0;
            selected = -1;
        } else if (k == 0x101 || k == 0x102) {
            build_file_list();
            int at = -1;
            for (int i = 0; i < file_count; i++)
                if (file_list[i] == selected)
                    at = i;
            if (k == 0x102 && at < file_count - 1)
                at++;
            if (k == 0x101 && at > 0)
                at--;
            if (at >= 0) {
                selected = file_list[at];
                int rows = (windows[0].h - 225) / 44;
                if (at < file_scroll)
                    file_scroll = at;
                if (at >= file_scroll + rows)
                    file_scroll = at - rows + 1;
            }
        }
    }
}
void kernel_main(u32 magic, u32 info) {
    hardware_init(magic, info);
#ifndef MAYOS_LINUX
    net_init();
#endif
    fs_init();
    terminal_init();
    serial("MayOS: desktop ready\n");
    u32 last = 0;
    for (;;) {
        int key = 0;
        if (input_poll(&mx, &my, &buttons, &key)) {
            dirty = 1;
            if ((buttons & 1) && !(oldbuttons & 1))
                left_click();
            if ((buttons & 2) && !(oldbuttons & 2))
                right_click();
            if (!(buttons & 1)) {
                drag = -1;
                resizing = -1;
            }
            if (resizing >= 0) {
                Window *w = &windows[resizing];
                int min_width = resizing == 1 ? 800 : 590;
                int min_height = resizing == 1 ? 620 : 380;
                int width = mx - w->x + 8;
                int height = my - w->y + 8;
                if (width < min_width) width = min_width;
                if (height < min_height) height = min_height;
                if (width > W - w->x) width = W - w->x;
                if (height > H - 92 - w->y) height = H - 92 - w->y;
                if (width >= min_width && height >= min_height) {
                    w->w = width;
                    w->h = height;
                }
            }
            if (drag >= 0) {
                Window *w = &windows[drag];
                w->x = mx - dragx;
                w->y = my - dragy;
                if (w->x < 0)
                    w->x = 0;
                if (w->x > W - w->w)
                    w->x = W - w->w;
                if (w->y < 50)
                    w->y = 50;
                if (w->y > H - 90)
                    w->y = H - 90;
            }
            oldbuttons = buttons;
            if (key)
                key_event(key);
        }
        if (explorer_loading) {
#ifdef MAYOS_LINUX
            explorer_loading = platform_web_poll(explorer_response, sizeof(explorer_response));
#else
            explorer_loading = net_http_poll(explorer_response, sizeof(explorer_response));
#endif
            dirty = 1;
        }
        if (ticks / 50 != last) {
            last = ticks / 50;
            dirty = 1;
        }
        if (dirty)
            render();
#ifndef MAYOS_LINUX
        net_poll();
#endif
        platform_idle();
    }
}

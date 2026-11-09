#include "cat_image.h"
#include "core.h"
#include "font.h"
#ifdef MAYOS_LINUX
#include "explorer_history.h"
#endif
#define MAYOS_RELEASE "0.4.0-pre37"
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
           accent = 0xd0bcff, surface_high = 0x2b2930, on_accent = 0x381e72, selected_fill = 0x4f378b;
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
static void border(int x, int y, int w, int h, u32 c) {
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
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
    } else {
        /* network / globe */
        ring(cx, cy, size / 3, thick, c);
        stroke(x + m, cy, x + size - m, cy, thick, c);
        stroke(cx, y + m, cx, y + size - m, thick, c);
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
static int last_clicked = -1;
static int editor_file = -1, editor_changed = 0;
static char editor_buf[FILE_CAP];
static char output[180][100], command[160], history[160];
static int out_count, cmdlen, cwd = 0, scroll = 0;
static void notify(const char *s) {
    copy(notice, s, sizeof(notice));
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
        emit("theme light|dark  net status  reboot  shutdown");
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
        emit("Boot Linux for DHCP, DNS, IP diagnostics and HTTP preview.");
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
    const u32 dark_primary[] = {0xd0bcff, 0x80d5ce, 0xafc6ff, 0xffb4a6};
    const u32 light_primary[] = {0x6750a4, 0x006a64, 0x415f91, 0x904b40};
    bg = dark ? 0x141218 : 0xfffbfe;
    panel = dark ? 0x211f26 : 0xfff7ff;
    surface_high = dark ? 0x2b2930 : 0xf3edf7;
    ink = dark ? 0xe6e1e5 : 0x1d1b20;
    muted = dark ? 0xcac4d0 : 0x49454f;
    line = dark ? 0x49454f : 0xcac4d0;
    accent = dark ? dark_primary[accent_choice] : light_primary[accent_choice];
    on_accent = dark ? 0x381e72 : 0xffffff;
    selected_fill = dark ? 0x4f378b : 0xeaddff;
}
static void desktop(void) {
    /* Material 3: tonal canvas, navigation rail, compact status area. */
    for (int y = 0; y < H; y++)
        rect(0, y, W, 1, blend(dark ? 0x15131b : 0xf6f1fa,
                               dark ? 0x211e29 : 0xece6f3, (u32)y * 130 / H));
    rect(0, 0, W, 46, dark ? 0x211f26 : 0xf3edf7);
    rect(0, 45, W, 1, line);
    ring(30, 23, 10, 3, accent);
    text(50, 13, "MayOS", ink);
    text(157, 13, "Workspace", muted);
    text(786, 13, PLATFORM_BADGE, muted);
    roundrect(14, 60, 76, 508, panel);
    const char *names[] = {"文件", "设置", "终端", "回收站"};
    for (int i = 0; i < 4; i++) {
        int top = 77 + i * 100;
        if (windows[i == 3 ? 0 : i].open && focus == (i == 3 ? 0 : i) && !windows[focus].min)
            roundrect(22, top + 3, 60, 58, selected_fill);
        icon(32, top + 8, i, 44);
        text(i == 3 ? 30 : 38, top + 58, names[i], muted);
    }
    symbol(35, 489, 4, 28, ink);
    text(17, 535, "Explorer", muted);
    large_text(728, 594, "MayOS", muted);
    text(729, 640, "MATERIAL WORKSPACE / " MAYOS_RELEASE, muted);
}
static void frame(int id) {
    Window *w = &windows[id];
    rect(w->x + 5, w->y + 7, w->w, w->h, dark ? 0x141218 : 0xd8d1df);
    roundrect(w->x, w->y, w->w, w->h, panel);
    rect(w->x + 1, w->y + 1, w->w - 2, 40, surface_high);
    rect(w->x + 1, w->y + 41, w->w - 2, 1, line);
    symbol(w->x + 13, w->y + 10, id == 0 ? 0 : id == 1 ? 1 : id == 4 ? 4 : 2, 22, ink);
    text(w->x + 45, w->y + 10, w->title, focus == id ? ink : muted);
    int controls = w->x + w->w - 119;
    for (int i = 0; i < 3; i++)
        circle(controls + i * 36 + 17, w->y + 21, 12,
               surface_high);
    rect(controls + 11, w->y + 21, 11, 1, muted);
    border(controls + 48, w->y + 16, 10, 10, muted);
    text(w->x + w->w - 35, w->y + 10, "×", muted);
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
        u32 colors[] = {0xd0bcff, 0x80d5ce, 0xafc6ff, 0xffb4a6};
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
/* Orange shares this UI across both kernels. Linux provides the HTTP transport. */
static char explorer_query[72], explorer_bookmark[72];
#ifdef MAYOS_LINUX
static int explorer_loading;
static int explorer_scroll;
static ExplorerHistory explorer_history;
static char explorer_response[2048];
#endif
static void explorer_submit(void) {
#ifdef MAYOS_LINUX
    explorer_loading = platform_web_start(explorer_query);
    explorer_scroll = 0;
    if (explorer_loading) explorer_history_push(&explorer_history, explorer_query);
    platform_web_poll(explorer_response, sizeof(explorer_response));
    dirty = 1;
#else
    notify("MayKernel 尚无网络栈；请使用 Linux 启动项联网。");
#endif
}
static void explorer_draw(Window *w) {
    int x = w->x, y = w->y;
    text(x + 24, y + 58, "<   >   ↻", muted);
    roundrect(x + 120, y + 51, w->w - 250, 34, bg);
    text(x + 132, y + 58, "输入 http:// 地址 / ASCII", muted);
    button(x + w->w - 115, y + 53, 90, "收藏", 0);
    text(x + 25, y + 100, explorer_bookmark[0] ? explorer_bookmark : "收藏夹为空（仅本次会话）", muted);
    circle(x + 130, y + 207, 24, accent);
    circle(x + 130, y + 207, 18, panel);
    large_text(x + 177, y + 187, "MayOS Explorer", ink);
    roundrect(x + 100, y + 260, w->w - 200, 44, bg);
    text(x + 114, y + 272, explorer_query[0] ? explorer_query : "http://example.com/", ink);
    button(x + w->w - 92, y + 267, 65, "转到", 1);
#ifdef MAYOS_LINUX
    if (explorer_loading) button(x + w->w - 92, y + 312, 65, "停止", 0);
#endif
    text(x + 100, y + 337, "网络 / HTTP 文本预览。HTTPS 与完整浏览器尚未支持。", muted);
#ifdef MAYOS_LINUX
    char network_report[384];
    int has_network = platform_network_status(network_report, sizeof(network_report));
    text(x + 100, y + 358, has_network ? "IP 已获取 · DHCP 网络可用" : "网络初始化中 · 检查 DHCP 或网卡", muted);

    text(x + 100, y + 383, explorer_loading ? "HTTP 正在加载 / 10 秒超时" : "HTTP 文本预览 · 不支持 HTTPS / HTML 渲染", accent);
    const char *s = explorer_response;
    int cols = (w->w - 140) / 8;
    if (cols > 79) cols = 79;
    for (int row = 0; row < explorer_scroll && *s; row++) {
        int n = 0;
        while (*s && *s != '\n' && n < cols) { s++; n++; }
        if (*s == '\n') s++;
    }
    for (int row = 0; row < (w->h - 420) / 20 && *s; row++) {
        char part[80];
        int n = 0;
        while (*s && *s != '\n' && n < cols) part[n++] = *s++;
        if (*s == '\n') s++;
        part[n] = 0;
        text(x + 70, y + 414 + row * 20, part, muted);
    }
    text(x + 70, y + w->h - 22, "↑ ↓ 滚动响应 · 点击收藏栏打开 · 会话历史最多 8 项", muted);
#else
    text(x + 100, y + 383, "Orange / MayKernel 网络栈尚未实现", accent);
    text(x + 100, y + 417, "请选择 Linux 启动项使用 HTTP 文本预览。", muted);
#endif
}
static void taskbar(void) {
    rect(0, H - 46, W, 46, dark ? 0x211f26 : 0xf3edf7);
    rect(0, H - 46, W, 1, line);
    roundrect(12, H - 38, 84, 31, start_open ? selected_fill : surface_high);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            rect(24 + x * 7, H - 29 + y * 7, 4, 4, accent);
    text(46, H - 34, "应用", ink);
    for (int i = 0; i < WINDOW_COUNT; i++)
        if (windows[i].open) {
            int x = 110 + i * 126;
            int active = focus == i && !windows[i].min;
            rect(x, H - 38, 118, 31, active ? selected_fill : surface_high);
            text(x + 14, H - 34,
                 i == 0   ? "文件"
                 : i == 1 ? "系统设置"
                 : i == 2 ? ">_ 终端"
                          : i == 3 ? "文本编辑器" : "Explorer",
                 active ? ink : muted);
            if (active)
                rect(x + 12, H - 6, 94, 2, accent);
        }
    circle(772, H - 23, 3, disk_ready ? accent : 0x86a5b2);
    text(785, H - 33,
#ifdef MAYOS_LINUX
         "RAM SESSION",
#else
         disk_ready ? "MayFS / disk" : "RAM / session",
#endif
         0x87a0ae);
    u32 sec = ticks / 100;
    char clock[32], n[12];
    number(n, sec / 3600);
    copy(clock, n, 32);
    copy(clock + strlen(clock), ":", 32 - strlen(clock));
    u32 min = sec / 60 % 60;
    number(n, min);
    if (min < 10)
        copy(clock + strlen(clock), "0", 32 - strlen(clock));
    copy(clock + strlen(clock), n, 32 - strlen(clock));
    text(923, H - 33, clock, 0xc8dce4);
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
    const char *names[] = {"files 文件 文档", "settings 设置 关于", "terminal 终端 shell",
                           "trash 回收站"};
    return contains(names[id], search);
}
static void start_draw(void) {
    int x = 12, y = H - 424;
    roundrect(x + 3, y + 4, 326, 366, dark ? 0x141218 : 0xd8d1df);
    roundrect(x, y, 326, 366, panel);
    text(x + 22, y + 19, "应用程序 / Applications", ink);
    roundrect(x + 18, y + 57, 290, 34, bg);
    text(x + 30, y + 64, search[0] ? search : "搜索 / files, settings, terminal",
         search[0] ? ink : muted);
    const char *names[] = {"文件管理器", "设置", "终端", "回收站"};
    const char *desc[] = {"浏览文件与文件夹", "系统信息与外观", "命令与会话工具", "找回或删除文件"};
    for (int i = 0; i < 4; i++)
        if (search_match(i)) {
            icon(x + 24, y + 109 + i * 47, i, 31);
            text(x + 70, y + 105 + i * 47, names[i], ink);
            text(x + 70, y + 125 + i * 47, desc[i], muted);
        }
    rect(x + 18, y + 306, 290, 1, line);
    text(x + 25, y + 328, "may / 本地账户", muted);
    text(x + 213, y + 328, "重启", ink);
    text(x + 270, y + 328, "关机", ink);
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
static void cursor(void) {
    for (int y = 0; y < 20; y++)
        for (int x = 0; x <= y / 2; x++)
            rect(mx + x, my + y, 1, 1, x == 0 || x == y / 2 || y == 19 ? 0x223149 : 0xffffff);
    rect(mx + 6, my + 14, 3, 9, 0x223149);
    rect(mx + 7, my + 15, 1, 6, 0xffffff);
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
    if (start_open)
        start_draw();
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
    if (my >= H - 46) {
        if (mx < 100) {
            start_open = !start_open;
            search[0] = 0;
            return;
        }
        for (int i = 0; i < WINDOW_COUNT; i++)
            if (windows[i].open && inside(mx, my, 110 + i * 126, H - 38, 118, 31)) {
                if (focus == i && !windows[i].min)
                    windows[i].min = 1;
                else
                    raise_window(i);
                return;
            }
        start_open = 0;
        return;
    }
    if (start_open) {
        int x = 12, y = H - 424;
        if (inside(mx, my, x, y, 326, 366)) {
            for (int i = 0; i < 4; i++)
                if (search_match(i) && inside(mx, my, x + 16, y + 100 + i * 47, 294, 46)) {
                    if (i == 3) {
                        folder = 3;
                        file_scroll = 0;
                        selected = -1;
                        raise_window(0);
                    } else
                        raise_window(i);
                    return;
                }
            if (my >= y + 320) {
                if (mx > x + 267) {
                    fs_sync();
                    poweroff();
                } else if (mx > x + 210) {
                    fs_sync();
                    reboot();
                }
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
#ifdef MAYOS_LINUX
            if (inside(rx, ry, 18, 50, 90, 36)) {
                const char *url = 0;
                if (rx < 48) url = explorer_history_move(&explorer_history, -1);
                else if (rx < 80) url = explorer_history_move(&explorer_history, 1);
                else if (explorer_history.count) url = explorer_history.urls[explorer_history.position];
                if (url) {
                    copy(explorer_query, url, sizeof(explorer_query));
                    explorer_loading = platform_web_start(explorer_query);
                    explorer_scroll = 0;
                    platform_web_poll(explorer_response, sizeof(explorer_response));
                }
                return;
            }
#endif
            if (inside(rx, ry, 20, 96, w->w - 40, 28) && explorer_bookmark[0]) {
                copy(explorer_query, explorer_bookmark, sizeof(explorer_query));
                explorer_submit();
                return;
            }
            if (inside(rx, ry, w->w - 115, 53, 90, 30)) {
                copy(explorer_bookmark, explorer_query, sizeof(explorer_bookmark));
                notify(explorer_query[0] ? "已收藏到本次会话" : "请先输入搜索或链接");
            } else if (inside(rx, ry, w->w - 92, 267, 65, 30)) explorer_submit();
#ifdef MAYOS_LINUX
            else if (explorer_loading && inside(rx, ry, w->w - 92, 312, 65, 30)) {
                platform_web_cancel();
                explorer_loading = 0;
                platform_web_poll(explorer_response, sizeof(explorer_response));
            }
#endif
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
    if (inside(mx, my, 12, 486, 76, 72)) {
        raise_window(4);
        return;
    }
    for (int i = 0; i < 4; i++)
        if (inside(mx, my, 20, 76 + i * 100, 76, 88)) {
            if (i == 3) {
                folder = 3;
                file_scroll = 0;
                selected = -1;
                raise_window(0);
            } else
                raise_window(i);
            return;
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
    }
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
        start_open = context_open = 0;
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
            for (int i = 0; i < 4; i++)
                if (search_match(i)) {
                    if (i == 3) {
                        folder = 3;
                        raise_window(0);
                    } else
                        raise_window(i);
                    break;
                }
        }
        return;
    }
    if (focus == 4 && windows[4].open && !windows[4].min) {
#ifdef MAYOS_LINUX
        if (k == 0x101 || k == 0x102) {
            if (k == 0x101 && explorer_scroll > 0) explorer_scroll--;
            if (k == 0x102) {
                int cols = (windows[4].w - 140) / 8, rows = 0;
                if (cols > 79) cols = 79;
                const char *s = explorer_response;
                while (*s) {
                    int n = 0;
                    while (*s && *s != '\n' && n < cols) { s++; n++; }
                    if (*s == '\n') s++;
                    rows++;
                }
                int visible = (windows[4].h - 420) / 20;
                if (explorer_scroll < rows - visible) explorer_scroll++;
            }
            return;
        }
#endif
        int n = strlen(explorer_query);
        if (k == '\n') explorer_submit();
        else if (k == '\b' && n) explorer_query[n - 1] = 0;
        else if (k >= 32 && k < 127 && n < 60) {
            explorer_query[n] = k;
            explorer_query[n + 1] = 0;
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
            if (!(buttons & 1))
                drag = -1;
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
#ifdef MAYOS_LINUX
        if (explorer_loading) {
            explorer_loading = platform_web_poll(explorer_response, sizeof(explorer_response));
            dirty = 1;
        }
#endif
        if (ticks / 50 != last) {
            last = ticks / 50;
            dirty = 1;
        }
        if (dirty)
            render();
        platform_idle();
    }
}

#include "core.h"
#include "io.h"
void *memset(void *p, int v, u32 n) {
    u8 *d = p;
    while (n--)
        *d++ = v;
    return p;
}
void *memcpy(void *p, const void *s, u32 n) {
    u8 *d = p;
    const u8 *b = s;
    while (n--)
        *d++ = *b++;
    return p;
}
u32 strlen(const char *s) {
    u32 n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return (u8)*a - (u8)*b;
}
void copy(char *d, const char *s, int n) {
    int i = 0;
    for (; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
void number(char *b, u32 n) {
    char t[12];
    int i = 0, j = 0;
    do {
        t[i++] = '0' + n % 10;
        n /= 10;
    } while (n);
    while (i)
        b[j++] = t[--i];
    b[j] = 0;
}
void serial(const char *s) {
    while (*s) {
        for (int i = 0; i < 100000 && !(inb(0x3fd) & 32); i++)
            ;
        outb(0x3f8, *s++);
    }
}
volatile u32 ticks;
u32 memory_mb, free_pages;
char cpu_name[64];
static volatile u32 *framebuffer;
static u32 pitch, redpos, greenpos, bluepos;
static u8 pages[32768];
static u32 page_count;
extern char kernel_end;
void *page_alloc(void) {
    for (u32 i = 0; i < page_count; i++)
        if (!pages[i]) {
            pages[i] = 1;
            free_pages--;
            void *p = (void *)(i * 4096);
            memset(p, 0, 4096);
            return p;
        }
    return 0;
}
void page_free(void *p) {
    if (!p || ((u32)p & 4095))
        return;
    u32 i = (u32)p / 4096;
    if (i < page_count && pages[i] == 1) {
        pages[i] = 0;
        free_pages++;
    }
}
static void memory_init(const u8 *m, u32 flags) {
    /* Multiboot mem_upper describes only the first contiguous range. EFI
       firmware commonly leaves a hole near 8 MiB, so use its full memory map.
       Bitmap values: 0 = available, 1 = allocated, 2 = permanently reserved. */
    memset(pages, 2, sizeof(pages));
    page_count = 0;
    memory_mb = 0;
    int valid_map = 0;
    u32 length = 0;
    const u8 *map = 0;
    if (flags & (1 << 6)) {
        length = *(const u32 *)(m + 44);
        map = (const u8 *)*(const u32 *)(m + 48);
        valid_map = map && length >= 24;
        for (u32 offset = 0; valid_map && offset < length;) {
            if (length - offset < 4) {
                valid_map = 0;
                break;
            }
            u32 size = *(const u32 *)(map + offset);
            if (size < 20 || size > length - offset - 4) {
                valid_map = 0;
                break;
            }
            offset += size + 4;
        }
    }
    if (valid_map) {
        u64 available_bytes = 0;
        /* Apply reserved ranges last, even if a firmware map overlaps them. */
        for (int pass = 0; pass < 2; pass++) {
            for (u32 offset = 0; offset < length;) {
                const u8 *entry = map + offset;
                u32 size = *(const u32 *)entry;
                u64 base = *(const u64 *)(entry + 4);
                u64 span = *(const u64 *)(entry + 12);
                u32 type = *(const u32 *)(entry + 20);
                offset += size + 4;
                if ((pass == 0) != (type == 1) || base >= 0x100000000ULL)
                    continue;
                u64 end = base + span;
                if (end < base || end > 0x100000000ULL)
                    end = 0x100000000ULL;
                if (type == 1)
                    available_bytes += end - base;
                u64 first = type == 1 ? (base + 4095) / 4096 : base / 4096;
                u64 last = type == 1 ? end / 4096 : (end + 4095) / 4096;
                if (last > sizeof(pages))
                    last = sizeof(pages);
                if (type == 1 && last > page_count)
                    page_count = (u32)last;
                for (u64 i = first; i < last; i++)
                    pages[(u32)i] = type == 1 ? 0 : 2;
            }
        }
        memory_mb = (u32)(available_bytes / (1024 * 1024));
        serial("MayOS: using Multiboot memory map\n");
    } else if (flags & 1) {
        /* Older BIOS loaders may provide only the legacy memory fields. */
        memory_mb = (u32)(((u64)*(const u32 *)(m + 8) + 1024) / 1024);
        page_count = memory_mb >= 128 ? sizeof(pages) : memory_mb * 256;
        memset(pages, 0, page_count);
        serial("MayOS: using legacy contiguous memory range\n");
    }
    u32 reserved = ((u32)&kernel_end + 4095) / 4096;
    free_pages = 0;
    for (u32 i = 0; i < page_count; i++) {
        if (i < reserved)
            pages[i] = 2;
        if (pages[i] == 0)
            free_pages++;
    }
    char value[12];
    serial("MayOS: available memory MiB=");
    number(value, memory_mb);
    serial(value);
    serial("; free managed pages=");
    number(value, free_pages);
    serial(value);
    serial("\n");
}
struct Gate {
    u16 low, cs;
    u8 zero, attr;
    u16 high;
} __attribute__((packed));
static struct Gate idt[256];
extern void timer_stub(void);
void timer_interrupt(void) {
    ticks++;
    outb(0x20, 0x20);
}
static void timer_init(void) {
    u32 a = (u32)timer_stub;
    idt[32] = (struct Gate){a & 65535, 8, 0, 0x8e, a >> 16};
    struct {
        u16 limit;
        u32 base;
    } __attribute__((packed)) p = {sizeof(idt) - 1, (u32)idt};
    __asm__ volatile("lidt %0" ::"m"(p));
    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, 32);
    outb(0xa1, 40);
    outb(0x21, 4);
    outb(0xa1, 2);
    outb(0x21, 1);
    outb(0xa1, 1);
    outb(0x21, 0xfe);
    outb(0xa1, 0xff);
    outb(0x43, 0x36);
    outb(0x40, 11932 & 255);
    outb(0x40, 11932 >> 8);
    __asm__ volatile("sti");
}
static int wait_write(void) {
    for (u32 i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2))
            return 1;
    return 0;
}
static int wait_read(void) {
    for (u32 i = 0; i < 100000; i++)
        if (inb(0x64) & 1)
            return 1;
    return 0;
}
static void mouse_cmd(u8 v) {
    wait_write();
    outb(0x64, 0xd4);
    wait_write();
    outb(0x60, v);
    if (wait_read())
        inb(0x60);
}
static void ps2_init(void) {
    wait_write();
    outb(0x64, 0xa8);
    wait_write();
    outb(0x64, 0xae);
    wait_write();
    outb(0x64, 0x20);
    u8 cfg = wait_read() ? inb(0x60) : 0x40;
    cfg = (cfg | 0x40) & ~0x33;
    wait_write();
    outb(0x64, 0x60);
    wait_write();
    outb(0x60, cfg);
    mouse_cmd(0xf6);
    mouse_cmd(0xf4);
}
void hardware_init(u32 magic, u32 info) {
    outb(0x3fb, 0x80);
    outb(0x3f8, 1);
    outb(0x3f9, 0);
    outb(0x3fb, 3);
    outb(0x3fa, 0xc7);
    serial("MayOS: entering custom x86 kernel\n");
    if (magic != 0x2badb002) {
        serial("Invalid multiboot magic\n");
        for (;;)
            __asm__("hlt");
    }
    u8 *m = (u8 *)info;
    u32 flags = *(u32 *)m;
    if (!(flags & (1 << 12)) || *(u32 *)(m + 100) != W || *(u32 *)(m + 104) != H || m[108] != 32 ||
        m[109] != 1) {
        serial("Unsupported framebuffer: require 1024x768x32 RGB\n");
        for (;;)
            __asm__("hlt");
    }
    u64 framebuffer_address = *(u64 *)(m + 88);
    if (framebuffer_address > 0xffffffffULL || *(u32 *)(m + 96) < W * 4 ||
        framebuffer_address + (u64)*(u32 *)(m + 96) * H > 0x100000000ULL) {
        serial("Unsupported framebuffer: outside 32-bit addressable range\n");
        for (;;)
            __asm__("hlt");
    }
    framebuffer = (u32 *)(u32)framebuffer_address;
    pitch = *(u32 *)(m + 96) / 4;
    redpos = m[112];
    greenpos = m[114];
    bluepos = m[116];
    memory_init(m, flags);
    u32 a, b, c, d;
    for (u32 i = 0; i < 3; i++) {
        a = 0x80000002 + i;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
        memcpy(cpu_name + i * 16, &a, 4);
        memcpy(cpu_name + i * 16 + 4, &b, 4);
        memcpy(cpu_name + i * 16 + 8, &c, 4);
        memcpy(cpu_name + i * 16 + 12, &d, 4);
    }
    cpu_name[48] = 0;
    ps2_init();
    timer_init();
    serial("MayOS: framebuffer, memory, PIT and PS/2 ready\n");
}
void present(u32 *p) {
    for (u32 y = 0; y < H; y++)
        for (u32 x = 0; x < W; x++) {
            u32 c = p[y * W + x];
            framebuffer[y * pitch + x] =
                ((c >> 16 & 255) << redpos) | ((c >> 8 & 255) << greenpos) | ((c & 255) << bluepos);
        }
}
static u8 cmos_read(u8 register_number) {
    outb(0x70, (u8)(register_number | 0x80));
    return inb(0x71);
}
static u8 from_bcd(u8 value) {
    return (u8)((value >> 4) * 10 + (value & 15));
}
void platform_clock(char *buffer) {
    u8 flags = cmos_read(0x0b);
    u8 minute = cmos_read(0x02);
    u8 hour = cmos_read(0x04);
    int pm = !!(hour & 0x80);
    hour &= 0x7f;
    if (!(flags & 0x04)) {
        hour = from_bcd(hour);
        minute = from_bcd(minute);
    }
    if (!(flags & 0x02)) {
        hour = (u8)(hour % 12 + (pm ? 12 : 0));
    }
    buffer[0] = (char)('0' + hour / 10);
    buffer[1] = (char)('0' + hour % 10);
    buffer[2] = ':';
    buffer[3] = (char)('0' + minute / 10);
    buffer[4] = (char)('0' + minute % 10);
    buffer[5] = 0;
}
void poweroff(void) {
    serial("MayOS: power off\n");
    outw(0x604, 0x2000);
    outw(0xb004, 0x2000);
}
void reboot(void) {
    wait_write();
    outb(0x64, 0xfe);
}
static const char normal[] =
    "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char shifted[] =
    "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
int input_poll(int *mx, int *my, int *buttons, int *key) {
    static u8 packet[3], n = 0;
    static int shift = 0, ext = 0, ctrl = 0;
    *key = 0;
    int changed = 0;
    for (int k = 0; k < 128 && (inb(0x64) & 1); k++) {
        u8 status = inb(0x64), v = inb(0x60);
        if (status & 32) {
            if (!n && !(v & 8))
                continue;
            packet[n++] = v;
            if (n == 3) {
                n = 0;
                if (!(packet[0] & 0xc0)) {
                    int dx = (int)packet[1] - ((packet[0] & 16) ? 256 : 0),
                        dy = (int)packet[2] - ((packet[0] & 32) ? 256 : 0);
                    *mx += dx;
                    *my -= dy;
                    if (*mx < 0)
                        *mx = 0;
                    if (*mx >= W)
                        *mx = W - 1;
                    if (*my < 0)
                        *my = 0;
                    if (*my >= H)
                        *my = H - 1;
                    *buttons = packet[0] & 7;
                    changed = 1;
                }
            }
        } else {
            if (v == 0xe0) {
                ext = 1;
                continue;
            }
            if (v == 29) {
                ctrl = 1;
                continue;
            }
            if (v == 157) {
                ctrl = 0;
                continue;
            }
            if (v == 59) {
                *key = 0x105;
                return 1;
            }
            if (v == 42 || v == 54) {
                shift = 1;
                continue;
            }
            if (v == 170 || v == 182) {
                shift = 0;
                continue;
            }
            if (v & 128) {
                ext = 0;
                continue;
            }
            if (ext) {
                ext = 0;
                if (v == 72)
                    *key = 0x101;
                if (v == 80)
                    *key = 0x102;
                if (v == 83)
                    *key = 0x103;
                if (v == 91)
                    *key = 0x104;
            } else if (v < sizeof(normal) - 1)
                *key = shift ? shifted[v] : normal[v];
            if (ctrl && *key == 's')
                *key = 0x106;
            if (*key)
                return 1;
        }
    }
    return changed;
}

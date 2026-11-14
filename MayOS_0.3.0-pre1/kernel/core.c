#include "core.h"
#include "io.h"
#include "framebuffer.h"
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
/* A fatal CPU exception must leave a serial diagnosis, not silently triple-fault.
 * This is not process isolation or exception recovery. */
static void serial_hex64(u64 value) {
    static const char digits[] = "0123456789abcdef";
    char out[19];
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++)
        out[i + 2] = digits[(value >> ((15 - i) * 4)) & 15];
    out[18] = 0;
    serial(out);
}
void kernel_exception(u64 vector, u64 error, u64 rip) {
    char number_buf[12];
    serial("MayOS: FATAL EXCEPTION vector=");
    number(number_buf, (u32)vector);
    serial(number_buf);
    serial(" error=");
    serial_hex64(error);
    serial(" rip=");
    serial_hex64(rip);
    if (vector == 14) {
        u64 fault_address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));
        serial(" cr2=");
        serial_hex64(fault_address);
    }
    serial("\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
volatile u32 ticks;
u32 memory_mb, free_pages;
char cpu_name[64];
static MaySurface display;
static u8 pages[32768];
static u32 page_count;
extern char kernel_end;
void *page_alloc(void) {
    for (u32 i = 0; i < page_count; i++)
        if (!pages[i]) {
            pages[i] = 1;
            free_pages--;
            void *p = (void *)(uptr)((u64)i * 4096);
            memset(p, 0, 4096);
            return p;
        }
    return 0;
}
void page_free(void *p) {
    if (!p || ((uptr)p & 4095))
        return;
    uptr page = (uptr)p / 4096;
    if (page >= page_count) return;
    u32 i = (u32)page;
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
        map = (const u8 *)(uptr)*(const u32 *)(m + 48);
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
                if ((pass == 0) != (type == 1))
                    continue;
                u64 end = base + span;
                if (end < base)
                    end = ~0ULL;
                /* Memory reporting counts all RAM, including above 4 GiB.
                   The early physical allocator remains restricted to 128 MiB. */
                if (type == 1)
                    available_bytes += end - base;
                u64 first = base / 4096 + (type == 1 && (base & 4095) != 0);
                u64 last = end / 4096 + (type != 1 && (end & 4095) != 0);
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
    u32 reserved = (u32)(((uptr)&kernel_end + 4095) / 4096);
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
/* Native 64-bit IDT gates: 16 bytes with a full 64-bit handler offset. */
struct Gate {
    u16 low, cs;
    u8 ist, attr;
    u16 middle;
    u32 high, zero;
} __attribute__((packed));
static struct Gate idt[256];
extern void timer_stub(void);
extern void (*const mayos_exception_stubs[32])(void);
void timer_interrupt(void) {
    ticks++;
    outb(0x20, 0x20);
}
static void timer_init(void) {
    for (u32 vector = 0; vector < 32; vector++) {
        uptr handler = (uptr)mayos_exception_stubs[vector];
        idt[vector] = (struct Gate){(u16)handler, 8, 0, 0x8e,
            (u16)(handler >> 16), (u32)(handler >> 32), 0};
    }
    uptr a = (uptr)timer_stub;
    idt[32] = (struct Gate){(u16)a, 8, 0, 0x8e, (u16)(a >> 16), (u32)(a >> 32), 0};
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) p = {sizeof(idt) - 1, (uptr)idt};
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
    serial("MayOS: entering custom x86_64 kernel\n");
    serial("MayOS: CPU long mode active (64-bit)\n");
    if (magic != 0x2badb002) {
        serial("Invalid multiboot magic\n");
        for (;;)
            __asm__("hlt");
    }
    u8 *m = (u8 *)(uptr)info;
    u32 flags = *(u32 *)m;
    if (!(flags & (1 << 12)) ||
        m[109] != 1) {
        serial("Unsupported framebuffer: firmware RGB mode required\n");
        for (;;)
            __asm__("hlt");
    }
    u64 framebuffer_address = *(u64 *)(m + 88);
    display = (MaySurface){.width=*(u32 *)(m + 100), .height=*(u32 *)(m + 104),
        .pitch=*(u32 *)(m + 96), .bpp=m[108], .red=m[112], .red_bits=m[113],
        .green=m[114], .green_bits=m[115], .blue=m[116], .blue_bits=m[117]};
    if (framebuffer_address >= (64ULL << 30) ||
        !surface_valid(&display, (64ULL << 30) - framebuffer_address)) {
        serial("Unsupported framebuffer: layout, pitch, size or 32-bit address range\n");
        for (;;)
            __asm__("hlt");
    }
    display.data = (u8 *)(uptr)framebuffer_address;
    char display_number[12];
    serial("MayOS: firmware framebuffer "); number(display_number, display.width); serial(display_number); serial("x");
    number(display_number, display.height); serial(display_number); serial("x"); number(display_number, display.bpp); serial(display_number); serial("\n");
    memory_init(m, flags);
    u32 a, b, c, d;
    a = 0x80000000U;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (a < 0x80000004U) copy(cpu_name, "Generic x86 (CPUID)", sizeof(cpu_name));
    else for (u32 i = 0; i < 3; i++) {
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
#ifdef MAYOS_EXCEPTION_SELFTEST
    serial("MayOS: intentional invalid-opcode exception self-test\n");
    __asm__ volatile("ud2");
#endif
    serial("MayOS: framebuffer, memory, PIT and PS/2 ready\n");
}
void present(u32 *p) {
    surface_present(&display, p);
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

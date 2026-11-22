#ifndef CORE_H
#define CORE_H
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
/* Host-size unsigned pointer type, including the freestanding x86_64 ABI. */
typedef unsigned long uptr;
#define W 1280
#define H 720
#define MAYOS_KEY_LEFT 0x108
#define MAYOS_KEY_RIGHT 0x109
#define MAYOS_KEY_IME 0x10a
#define FS_MAX 48
#define FILE_CAP 4096
#ifdef MAYOS_LINUX
#include <string.h>
/* Keep freestanding helper names separate from libc's size_t ABI. */
#define memset mayos_memset
#define memcpy mayos_memcpy
#define strlen mayos_strlen
#define strcmp mayos_strcmp
#define reboot mayos_reboot
const char *platform_kernel(void);
int platform_web_start(const char *url);
void platform_web_cancel(void);
int platform_web_poll(char *destination, int capacity);
int platform_network_status(char *destination, int capacity);
int platform_fastfetch(char *destination, int capacity);
void platform_idle(void);
#else
static inline const char *platform_kernel(void) { return "MayKernel x86_64 (custom)"; }
#ifdef MAYOS_RUST_CORE
void platform_idle(void);
#else
static inline void platform_idle(void) { __asm__ volatile("hlt"); }
#endif
#endif
void *memset(void *, int, u32);
void *memcpy(void *, const void *, u32);
u32 strlen(const char *);
int strcmp(const char *, const char *);
void copy(char *, const char *, int);
void serial(const char *);
void number(char *, u32);
extern volatile u32 ticks;
extern u32 memory_mb, free_pages;
extern char cpu_name[64];
void *page_alloc(void);
void page_free(void *);
#ifndef MAYOS_LINUX
int page_stress_test(void);
#endif
void hardware_init(u32, u32);
void boot_splash_begin(const char *kernel);
void boot_splash_log(const char *message);
void boot_splash_step(unsigned int completed, const char *message);
void boot_splash_finish(const char *message);
int input_poll(int *, int *, int *, int *);
void poweroff(void);
void reboot(void);
void present(u32 *);
typedef struct {
    u32 width, height, bpp, refresh_hz, scale_percent;
    int refresh_known, scale_mode;
} MayDisplayInfo;
void display_get_info(MayDisplayInfo *);
void display_set_scale_mode(int);
typedef struct {
    u32 used, dir, parent, size;
    char name[48];
    u8 data[FILE_CAP];
} File;
extern File files[FS_MAX];
extern int disk_ready;
int platform_ram_only(void);
void fs_init(void);
/* Separate checksummed record in the unused tail of a MayFS snapshot. */
int fs_prefs_read(u8 *destination, u32 bytes);
int fs_prefs_write(const u8 *source, u32 bytes);
#ifndef MAYOS_LINUX
int fs_installed_setup_pending(void);
#endif
int fs_sync(void);
int fs_find(int, const char *);
int fs_create(int, const char *, int);
int fs_remove(int);
int fs_move(int, int);
#endif

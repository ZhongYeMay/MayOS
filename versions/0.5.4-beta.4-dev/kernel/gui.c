#include "cat_image.h"
#include "design_palette.h"
#include "fluent_pointer.h"
#include "boot_logo.h"
#include "core.h"
#include "cursor.h"
#include "wallpaper.h"
#include "font.h"
#include "locale.h"
#define MAY_IME_RENDER
#include "ime.h"
#undef MAY_IME_RENDER
#include "procfs.h"
#include "task_manager.h"
#include "material_icons.h"
#ifndef MAYOS_LINUX
#include "galgame.h"
#include "hda.h"
#include "nvme.h"
#include "pci.h"
#include "xhci.h"
#endif
#include "explorer_history.h"
#include "release_feed.h"
#include "version.h"
#ifndef MAYOS_LINUX
#include "update.h"
#include "update_stage_disk.h"
#include "rust_bridge.h"
#include "update_stream_integrity.h"
#include "net.h"
#include "net_stream.h"
#include "security_tls.h"
#include "security_tls_provider.h"
#include "security_trusted_time.h"
#ifdef MAYOS_TLS_WIRE_DIAGNOSTIC
extern int may_tls_wire_test(const char *scenario, const char *port);
extern void may_tls_wire_poll(void);
#endif
#ifdef MAYOS_TLS_X509_DIAGNOSTIC
extern int may_tls_x509_fixture_selftest(void);
#endif
#endif
#define MAYOS_RELEASE MAYOS_VERSION
#ifdef MAYOS_LINUX
#define PLATFORM_SHORT "Linux / x86_64"
#define PLATFORM_BADGE "LINUX SESSION"
#define PLATFORM_ARCH "x86_64 / userspace"
#else
#define PLATFORM_SHORT "MayKernel / x86_64"
#define PLATFORM_BADGE "CUSTOM KERNEL"
#define PLATFORM_ARCH "x86_64 / 64-bit"
#endif
static u32 pixels[W * H];
/* MayOS 0.5.0: approved pure-black/charcoal visual system. */
static u32 bg = 0x050506, panel = 0x080809, ink = 0xf5f5f6, muted = 0xa1a1a5, line = 0x303033,
           accent = 0xececef, surface_high = 0x18181b, on_accent = 0x111113, selected_fill = 0x29292d;
static int dark = 1, accent_choice = 0;
enum { DESKTOP_GRADIENT, DESKTOP_SOLID, DESKTOP_GRID, DESKTOP_BING, DESKTOP_STYLE_COUNT };
static int desktop_style = DESKTOP_BING, pointer_size = 0, show_brand = 1, high_contrast = 0;
static char wallpaper_date[9];
static u32 daily_wallpaper[W * H];
_Static_assert(W == MAY_WALLPAPER_WIDTH && H == MAY_WALLPAPER_HEIGHT, "wallpaper dimensions");
static char profile_name[21] = "may";
static enum MayLocale display_language = MAY_ZH_HANS;
static enum MayLocale language_choice = MAY_ZH_HANS;
static int language_save_failed;
#define L(key) (translations[MAY_MSG_##key][display_language])

static const char locale_file[] = "MayOS-Language.txt";
static const char locale_header[] = "MAYLANG1\n";

static int locale_record(const File *file) {
    u32 size = sizeof(locale_header) - 1;
    if (file->dir || file->size <= size || file->size >= size + 16) return 0;
    for (u32 i = 0; i < size; i++)
        if (file->data[i] != (u8)locale_header[i]) return 0;
    return 1;
}

static void locale_load(void) {
    int index = fs_find(0, locale_file);
    if (index < 0 || !locale_record(&files[index])) return;
    const File *file = &files[index];
    u32 offset = sizeof(locale_header) - 1;
    display_language = MAY_EN;
    for (int i = 0; i < MAY_LOCALE_COUNT; i++) {
        const char *tag = languages[i].tag;
        if (file->size - offset != strlen(tag)) continue;
        u32 k = 0;
        while (tag[k] && file->data[offset + k] == (u8)tag[k]) k++;
        if (!tag[k]) {
            display_language = (enum MayLocale)i;
            break;
        }
    }
    serial("MayOS: display language loaded: ");
    serial(languages[display_language].tag);
    serial("\n");
}

/* Stage a regular MayFS file so older builds retain it during updates. */
typedef struct {
    int index, created;
    u32 old_size;
    u8 old_record[sizeof(locale_header) + 16];
} LocaleEdit;

static int locale_stage(enum MayLocale locale, LocaleEdit *edit) {
    edit->index = fs_find(0, locale_file);
    if (edit->index < 0) {
        edit->index = fs_create(0, locale_file, 0);
        if (edit->index < 0) return 0;
        edit->created = 1;
    } else if (!locale_record(&files[edit->index])) {
        return 0;
    }
    File *file = &files[edit->index];
    edit->old_size = file->size;
    u32 offset = sizeof(locale_header) - 1;
    const char *tag = languages[locale].tag;
    u32 tag_size = strlen(tag);
    u32 new_size = offset + tag_size;
    memcpy(edit->old_record, file->data, new_size);
    memcpy(file->data, locale_header, offset);
    memcpy(file->data + offset, tag, tag_size);
    file->size = new_size;
    return 1;
}

static void locale_unstage(const LocaleEdit *edit) {
    if (edit->created) fs_remove(edit->index);
    else {
        File *file = &files[edit->index];
        memcpy(file->data, edit->old_record, file->size);
        file->size = edit->old_size;
    }
}

static int sync_changes(void);

static void locale_apply(void) {
    if (language_choice == display_language) return;
    language_save_failed = 0;
    if (disk_ready) {
        LocaleEdit edit = {0};
        if (!locale_stage(language_choice, &edit)) {
            language_save_failed = 1;
            return;
        }
        if (!fs_sync()) {
            locale_unstage(&edit);
            language_save_failed = 1;
            return;
        }
    }
    display_language = language_choice;
    serial("MayOS: display language applied: ");
    serial(languages[display_language].tag);
    serial("\n");
}
#ifndef MAYOS_LINUX
static const char profile_file[] = "MayOS-Profile.txt";
static const char profile_header[] = "MAYPROFILE1\n";
static int profile_character(int key) {
    return (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
           (key >= '0' && key <= '9') || key == '-' || key == '_';
}
static int profile_valid(const File *file) {
    u32 start = sizeof(profile_header) - 1;
    if (file->dir || file->size <= start || file->size >= start + sizeof(profile_name)) return 0;
    for (u32 i = 0; i < start; i++)
        if (file->data[i] != (u8)profile_header[i]) return 0;
    for (u32 i = start; i < file->size; i++)
        if (!profile_character(file->data[i])) return 0;
    return 1;
}
static void profile_load(void) {
    int index = fs_find(0, profile_file);
    if (index < 0 || !profile_valid(&files[index])) return;
    u32 start = sizeof(profile_header) - 1;
    u32 length = files[index].size - start;
    memcpy(profile_name, files[index].data + start, length);
    profile_name[length] = 0;
}
#endif
static int draw_clip_active, draw_clip_left, draw_clip_top, draw_clip_right, draw_clip_bottom;
/* Saved style 2 remains the legacy custom style. The source is stored separately. */
enum { POINTER_PPM, POINTER_CUR, POINTER_ANI, POINTER_ISO, POINTER_SOURCES };
static const char *const cursor_source_names[] = {"Cursor.ppm", "Cursor.cur", "Cursor.ani", "ISO"};
enum { POINTER_DARK, POINTER_LIGHT, POINTER_CUSTOM };
static int cursor_style = POINTER_LIGHT, cursor_active_source, cursor_choice = POINTER_CUR, desktop_options;
static u32 cursor_started, cursor_preview_start;
static MayCursor custom_cursor, preview_cursor;
static void draw_pointer_frame(const MayCursorFrame *frame, int x, int y, int side);
/* Parse bounded, plain-text P3 image tokens; "#..." comments are supported. */
static int cursor_ppm_int(const u8 **at, const u8 *end) {
    while (*at < end) {
        u8 ch = **at;
        if (ch == '#') {
            while (*at < end && **at != '\n') (*at)++;
        } else if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r') {
            (*at)++;
        } else break;
    }
    if (*at >= end || **at < '0' || **at > '9') return -1;
    int n = 0;
    while (*at < end && **at >= '0' && **at <= '9') {
        n = n * 10 + (**at - '0');
        if (n > 65535) return -1;
        (*at)++;
    }
    return n;
}
static int cursor_ppm_load(MayCursor *out, int create_sample) {
    int i = fs_find(1, "Cursor.ppm");
    if (i < 0 && create_sample) {
        /* Create a sample only on explicit user selection, not at cold boot.
         * Pre-seeding files changed the original file manager row positions. */
        static const char sample[] =
            "P3\n"
            "8 8\n"
            "255\n"
            "255 0 255 255 0 255 20 184 166 20 184 166 20 184 166 20 184 166 255 0 255 255 0 255\n"
            "255 0 255 20 184 166 16 16 16 16 16 16 16 16 16 16 16 16 20 184 166 255 0 255\n"
            "20 184 166 16 16 16 255 255 255 255 255 255 255 255 255 255 255 255 16 16 16 20 184 166\n"
            "20 184 166 16 16 16 255 255 255 20 184 166 20 184 166 255 255 255 16 16 16 20 184 166\n"
            "20 184 166 16 16 16 255 255 255 20 184 166 20 184 166 255 255 255 16 16 16 20 184 166\n"
            "20 184 166 16 16 16 255 255 255 255 255 255 255 255 255 255 255 255 16 16 16 20 184 166\n"
            "255 0 255 20 184 166 16 16 16 16 16 16 16 16 16 16 16 16 20 184 166 255 0 255\n"
            "255 0 255 255 0 255 20 184 166 20 184 166 20 184 166 20 184 166 255 0 255 255 0 255\n";
        i = fs_create(1, "Cursor.ppm", 0);
        if (i < 0 || sizeof(sample) > FILE_CAP) return 0;
        copy((char *)files[i].data, sample, FILE_CAP);
        files[i].size = (u32)(sizeof(sample) - 1);
        sync_changes();
    }
    if (i < 0 || files[i].dir || files[i].size < 12 ||
        files[i].size >= FILE_CAP) return 0;
    const u8 *at = files[i].data;
    const u8 *end = at + files[i].size;
    if (at[0] != 'P' || at[1] != '3') return 0;
    at += 2;
    int width = cursor_ppm_int(&at, end);
    int height = cursor_ppm_int(&at, end);
    int peak = cursor_ppm_int(&at, end);
    if (width < 1 || width > 16 || height < 1 || height > 16 || peak != 255)
        return 0;
    MayCursorFrame *frame = &out->frames[0];
    for (int n = 0; n < width * height; n++) {
        int red = cursor_ppm_int(&at, end);
        int green = cursor_ppm_int(&at, end);
        int blue = cursor_ppm_int(&at, end);
        if (red < 0 || red > 255 || green < 0 || green > 255 ||
            blue < 0 || blue > 255) return 0;
        u32 rgb = (u32)red << 16 | (u32)green << 8 | (u32)blue;
        frame->pixels[n] = rgb == 0xff00ff ? 0 : 0xff000000u | rgb;
    }
    frame->width = (u16)width;
    frame->height = (u16)height;
    frame->hotspot_x = frame->hotspot_y = 0;
    out->count = out->steps = out->duration = 1;
    out->step_time[0] = 1;
    out->sequence[0] = 0;
    return 1;
}

static int cursor_source_load(int source, MayCursor *out, int sample) {
    out->count = out->steps = out->duration = 0;
    if (source == POINTER_PPM) return cursor_ppm_load(out, sample);
    if (source == POINTER_ISO) {
        u32 size;
        const u8 *data = platform_pointer_asset(&size);
        return may_cursor_decode(data, size, out);
    }
    int index = fs_find(1, cursor_source_names[source]);
    if (index < 0 || files[index].dir) return 0;
    const u8 *data = files[index].data;
    u32 size = files[index].size;
    if (size < 4 || (source == POINTER_CUR ? data[0] || data[1] || data[2] != 2 || data[3] :
                    data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')) return 0;
    return may_cursor_decode(data, size, out);
}

static const char pointer_record[] = "MayOS-Pointer.txt";
static const char pointer_magic[] = "MAYCUR1\n";

static int cursor_pref_valid(const File *file) {
    if (file->dir || file->size != sizeof(pointer_magic)) return 0;
    for (u32 i = 0; i < sizeof(pointer_magic) - 1; i++)
        if (file->data[i] != (u8)pointer_magic[i]) return 0;
    u8 source = file->data[sizeof(pointer_magic) - 1];
    return source >= '0' && source < '0' + POINTER_SOURCES;
}

static int cursor_source_saved(void) {
    int index = fs_find(0, pointer_record);
    return index >= 0 && cursor_pref_valid(&files[index]) ?
        files[index].data[sizeof(pointer_magic) - 1] - '0' : POINTER_PPM;
}

static int cursor_source_save(int source) {
    int index = fs_find(0, pointer_record);
    if (index < 0 && source == POINTER_PPM) return 1;
    if (index < 0) index = fs_create(0, pointer_record, 0);
    else if (!cursor_pref_valid(&files[index])) return 0;
    if (index < 0) return 0;
    memcpy(files[index].data, pointer_magic, sizeof(pointer_magic) - 1);
    files[index].data[sizeof(pointer_magic) - 1] = (u8)('0' + source);
    files[index].size = sizeof(pointer_magic);
    return 1;
}

static void cursor_select(int source, int sample) {
    cursor_choice = source;
    cursor_preview_start = ticks;
    cursor_source_load(source, &preview_cursor, sample);
}
#define MAYOS_PREFS_MAGIC 0x4d505246u
static int preferences_save(void) {
    MayDisplayInfo display_info;
    display_get_info(&display_info);
    u32 raw[8] = {MAYOS_PREFS_MAGIC, (u32)dark, (u32)accent_choice,
                  (u32)desktop_style, (u32)pointer_size, (u32)show_brand,
                  (u32)high_contrast | ((u32)cursor_style << 8) |
                      ((u32)display_info.scale_mode << 16), 1};
    /* Linux and diskless MayKernel keep the new values for this session. */
    return fs_prefs_write((const u8 *)raw, sizeof(raw));
}
static void preferences_load(void) {
    u32 raw[8];
    if (!fs_prefs_read((u8 *)raw, sizeof(raw)) || raw[0] != MAYOS_PREFS_MAGIC ||
        raw[7] != 1 || raw[1] > 1 || raw[2] >= 6 || raw[3] >= DESKTOP_STYLE_COUNT ||
        raw[4] >= 3 || raw[5] > 1 || (raw[6] & ~0x30301u) != 0 ||
        ((raw[6] >> 8) & 3u) >= 3)
        return;
    dark = (int)raw[1];
    accent_choice = (int)raw[2];
    desktop_style = (int)raw[3];
    pointer_size = (int)raw[4];
    show_brand = (int)raw[5];
    high_contrast = (int)(raw[6] & 1u);
    cursor_style = (int)((raw[6] >> 8) & 3u);
    display_set_scale_mode((int)((raw[6] >> 16) & 3u));
    if (cursor_style == POINTER_CUSTOM) {
        cursor_active_source = cursor_source_saved();
        if (cursor_source_load(cursor_active_source, &custom_cursor, 0)) {
            cursor_started = ticks;
            serial("MayOS: custom pointer restored: ");
            serial(cursor_source_names[cursor_active_source]);
            serial("\n");
        } else {
            cursor_style = POINTER_LIGHT;
            serial("MayOS: custom pointer unavailable; using default\n");
        }
    }
    serial("MayOS: desktop preferences applied\n");
}
static void preferences_reset(void) {
    dark = 1;
    accent_choice = 0;
    desktop_style = 0;
    pointer_size = 0;
    cursor_style = POINTER_LIGHT;
    show_brand = 1;
    high_contrast = 0;
    display_set_scale_mode(0);
    preferences_save();
}
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
    if (draw_clip_active) {
        if (x < draw_clip_left) { w -= draw_clip_left - x; x = draw_clip_left; }
        if (y < draw_clip_top) { h -= draw_clip_top - y; y = draw_clip_top; }
        if (x + w > draw_clip_right) w = draw_clip_right - x;
        if (y + h > draw_clip_bottom) h = draw_clip_bottom - y;
    }
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
    return -1;
}
static int glyph_width(int c) {
    return c < 0x250 || c == 0x2019 ? 8 : 16;
}
typedef struct {
    const u8 *pixels;
    int packed;
} MayGlyph;
static MayGlyph glyph_pixels(int cp, enum MayLocale locale) {
    int glyph_id = glyph(cp);
    if (glyph_id < 0) {
        int lo = 0, hi = (int)(sizeof(ime_glyph_codes) / sizeof(ime_glyph_codes[0])) - 1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            if (ime_glyph_codes[mid] == cp) return (MayGlyph){ime_glyphs[mid], 1};
            if (ime_glyph_codes[mid] < cp) lo = mid + 1;
            else hi = mid - 1;
        }
        /* The font atlas is bounded. Keep the actual UTF-8 intact on disk. */
        return (MayGlyph){font_bits[31], 0};
    }
    if (locale == MAY_JA && font_ja_index[glyph_id])
        return (MayGlyph){font_ja_bits[font_ja_index[glyph_id]], 0};
    if (locale == MAY_KO && font_ko_index[glyph_id])
        return (MayGlyph){font_ko_bits[font_ko_index[glyph_id]], 0};
    if (locale == MAY_ZH_HANT && font_tw_index[glyph_id])
        return (MayGlyph){font_tw_bits[font_tw_index[glyph_id]], 0};
    return (MayGlyph){font_bits[glyph_id], 0};
}
static u8 glyph_alpha(MayGlyph glyph, int index) {
    if (!glyph.packed) return glyph.pixels[index];
    return ((glyph.pixels[index / 4] >> (6 - 2 * (index % 4))) & 3) * 85;
}
static int text_width(const char *s) {
    int width = 0;
    while (*s) width += glyph_width(may_utf8_next(&s));
    return width;
}
static u32 blend(u32 b, u32 f, u32 a) {
    u32 r = (((b >> 16 & 255) * (255 - a) + (f >> 16 & 255) * a) / 255),
        g = (((b >> 8 & 255) * (255 - a) + (f >> 8 & 255) * a) / 255),
        v = (((b & 255) * (255 - a) + (f & 255) * a) / 255);
    return r << 16 | g << 8 | v;
}
/* Radius-aware antialiased native surfaces; same code path on MayKernel and Linux. */
static void soft_roundrect(int x, int y, int w, int h, int radius, u32 c) {
    if (w <= 0 || h <= 0) return;
    int r = radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (!r) { rect(x, y, w, h, c); return; }
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    for (int yy = 0; yy < r; yy++)
        for (int xx = 0; xx < r; xx++) {
            int dx = r - xx - 1, dy = r - yy - 1;
            int samples = 0;
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) {
                    int u = dx * 2 + sx, v = dy * 2 + sy;
                    if (u*u + v*v <= 4*r*r) samples++;
                }
            if (!samples) continue;
            int xs[2] = {x + xx, x + w - 1 - xx};
            int ys[2] = {y + yy, y + h - 1 - yy};
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    int px = xs[i], py = ys[j];
                    if (px < 0 || py < 0 || px >= W || py >= H) continue;
                    u32 *dst = &pixels[py * W + px];
                    *dst = samples == 4 ? c : blend(*dst, c, (u32)samples * 255 / 4);
                }
        }
}
static void ui_card(int x, int y, int w, int h, int r, u32 fill) {
    soft_roundrect(x, y, w, h, r, line);
    soft_roundrect(x + 1, y + 1, w - 2, h - 2, r - 1, fill);
}
#ifndef MAYOS_LINUX
/* Independently implemented software RGBA renderer for MayKernel game artwork.
   Pixel data is packed from the unchanged original WebP source at build time. */
static void maygame_blit(int x,int y,int width,int height,
                         const u8 *rgba,int srcw,int srch) {
    if(!rgba||width<=0||height<=0||srcw<=0||srch<=0)return;
    for(int dy=0;dy<height;dy++){
        int py=y+dy;
        if(py<0||py>=H)continue;
        int sy=(dy*srch)/height;
        for(int dx=0;dx<width;dx++){
            int px=x+dx;
            if(px<0||px>=W)continue;
            int sx=(dx*srcw)/width;
            const u8 *source=rgba+((u32)sy*(u32)srcw+(u32)sx)*4u;
            u32 alpha=source[3];
            if(!alpha)continue;
            u32 color=((u32)source[0]<<16)|((u32)source[1]<<8)|(u32)source[2];
            if(alpha==255) pixels[py*W+px]=color;
            else pixels[py*W+px]=blend(pixels[py*W+px],color,alpha);
        }
    }
}
#endif
static int text_face(int x, int y, const char *s, u32 color, enum MayLocale locale) {
    int start = x;
    while (*s) {
        int c = may_utf8_next(&s);
        if (c == '\n') {
            y += 22;
            x = start;
            continue;
        }
        MayGlyph bits = glyph_pixels(c, locale);
        int width = glyph_width(c);
        for (int j = 0; j < 20; j++)
            for (int i = 0; i < width; i++) {
                int px = x + i, py = y + j;
                if (px >= 0 && px < W && py >= 0 && py < H &&
                    (!draw_clip_active || (px >= draw_clip_left && px < draw_clip_right &&
                                           py >= draw_clip_top && py < draw_clip_bottom))) {
                    u8 a = glyph_alpha(bits, j * 16 + i);
                    if (a)
                        pixels[py * W + px] = blend(pixels[py * W + px], color, a);
                }
            }
        x += width;
    }
    return x;
}
static int text(int x, int y, const char *s, u32 color) {
    return text_face(x, y, s, color, display_language);
}
static void center_text(int x, int width, int y, const char *s, u32 color) {
    text(x + (width - text_width(s)) / 2, y, s, color);
}

#ifdef MAYOS_TASK_MANAGER
static void reference_text(int x, int y, const char *s, u32 color, int title);
void may_task_text(int x, int y, const char *label, u32 color) {
    reference_text(x, y - 2, label, color, 2);
}
void may_task_status(int x, int y, u32 status, u32 color) {
    static const char *const en[] = {"Sampling...", "No device detected", "Cannot read counters", "No GPU usage counter"};
    static const char *const zh[] = {"正在采样", "未检测到设备", "无法读取计数器", "驱动未提供占用率"};
    if (status >= sizeof(en) / sizeof(en[0])) return;
    if (display_language == MAY_ZH_HANS || display_language == MAY_ZH_HANT)
        text(x, y, zh[status], color);
    else
        reference_text(x, y - 2, en[status], color, 2);
}
void may_task_heading(int x, int y, const char *label, u32 color) {
    reference_text(x, y - 4, label, color, 1);
}
void may_task_cpu_name(char *destination, u32 size) {
    copy(destination, cpu_name, size);
}
void may_task_gpu_name(char *destination, u32 size) {
#ifdef MAYOS_LINUX
    copy(destination, "DRM graphics", size);
#else
    for (int index = 0; index < pci_device_count(); index++) {
        const PciDevice *device = pci_device_at(index);
        if (device->class_code != 3) continue;
        char label[32] = "PCI ";
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 0; i < 4; i++) {
            label[4 + i] = hex[(device->vendor_id >> (12 - i * 4)) & 15];
            label[9 + i] = hex[(device->device_id >> (12 - i * 4)) & 15];
        }
        label[8] = ':';
        copy(destination, label, size);
        return;
    }
    copy(destination, "Firmware framebuffer", size);
#endif
}
void may_task_selected_device(u32 index) {
    static const char *const devices[] = {"CPU", "Memory", "Disk", "Network", "GPU"};
    if (index >= sizeof(devices) / sizeof(devices[0])) return;
    serial("MayOS Task Manager: showing ");
    serial(devices[index]);
    serial("\n");
}
void may_task_rect(int x, int y, int w, int h, u32 color) {
    rect(x, y, w, h, color);
}
void may_task_card(int x, int y, int w, int h, int radius, u32 color) {
    soft_roundrect(x, y, w, h, radius, color);
}
#endif

enum { BOOT_LOG_ROWS = 8, BOOT_LOG_WIDTH = 106 };
static char boot_lines[BOOT_LOG_ROWS][BOOT_LOG_WIDTH + 1];
static char boot_line[BOOT_LOG_WIDTH + 1];
static unsigned int boot_line_length, boot_log_count;
static int boot_log_closed;
static u32 boot_splash_started;
static const char *boot_splash_kernel;

void boot_splash_log(const char *message) {
    if (boot_log_closed) return;
    for (; *message; message++) {
        unsigned char ch = (unsigned char)*message;
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (!boot_line_length) continue;
            boot_line[boot_line_length] = 0;
            if (boot_log_count == BOOT_LOG_ROWS) {
                for (unsigned int i = 1; i < BOOT_LOG_ROWS; i++)
                    memcpy(boot_lines[i - 1], boot_lines[i], sizeof(boot_line));
                boot_log_count--;
            }
            copy(boot_lines[boot_log_count++], boot_line, sizeof(boot_line));
            boot_line_length = 0;
        } else if (boot_line_length < BOOT_LOG_WIDTH) {
            boot_line[boot_line_length++] = ch == '\t' ? ' ' : (ch < 32 || ch > 126 ? '?' : (char)ch);
        }
    }
}

static void boot_draw_mask(int left, int top, int width, int height,
                           const u8 *mask, u32 foreground) {
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            pixels[(top + y) * W + left + x] =
                blend(0, foreground, mask[y * width + x]);
}

static void boot_draw_screen(const char *message, int show_log) {
    rect(0, 0, W, H, 0);
    if (show_log) {
        text(199, 167, "Startup log", MAY_DARK_INK);
        text(908, 167, boot_splash_kernel, MAY_DARK_MUTED);
        soft_roundrect(174, 203, 932, 255, 16, MAY_DARK_LINE);
        soft_roundrect(175, 204, 930, 253, 15, MAY_DARK_PANEL);
        for (unsigned int i = 0; i < boot_log_count; i++)
            text(199, 223 + (int)i * 25, boot_lines[i], MAY_DARK_MUTED);
        text(199, 469, message, MAY_DARK_MUTED);
    } else {
        boot_draw_mask((W - BOOT_LOGO_W) / 2, (H - BOOT_LOGO_H) / 2 - 46,
                       BOOT_LOGO_W, BOOT_LOGO_H, boot_logo, MAY_DARK_INK);
    }
    if (show_log) center_text(0, W, 673, "Tab  ·  MayOS", MAY_DARK_MUTED);
    present(pixels);
}

void boot_splash_step(const char *message) {
    if (boot_splash_kernel && !boot_log_closed) boot_draw_screen(message, 0);
}

void boot_splash_begin(const char *kernel) {
    boot_splash_kernel = kernel;
    boot_splash_started = ticks;
    boot_splash_step("Framebuffer ready");
    serial("MayOS: startup splash ready\n");
}

void boot_splash_finish(const char *message) {
    boot_splash_step(message);
    serial("MayOS: startup splash complete\n");
    u32 completed = ticks;
    int show_log = 0, x = W / 2, y = H / 2, mouse = 0;
    while (ticks - boot_splash_started < 130 || ticks - completed < 60) {
        int key = 0;
        if (input_poll(&x, &y, &mouse, &key) && key == '\t') {
            show_log = !show_log;
            boot_draw_screen(message, show_log);
        }
        platform_idle();
    }
    boot_log_closed = 1;
}

#ifndef MAYOS_LINUX
static int large_text(int x, int y, const char *s, u32 color) {
    while (*s) {
        int c = may_utf8_next(&s), width = glyph_width(c);
        MayGlyph bits = glyph_pixels(c, display_language);
        for (int j = 0; j < 20; j++)
            for (int i = 0; i < width; i++) {
                u8 a = glyph_alpha(bits, j * 16 + i);
                if (a)
                    for (int yy = 0; yy < 2; yy++)
                        for (int xx = 0; xx < 2; xx++) {
                            int px = x + i * 2 + xx, py = y + j * 2 + yy;
                            if (px >= 0 && px < W && py >= 0 && py < H &&
                                (!draw_clip_active || (px >= draw_clip_left && px < draw_clip_right &&
                                                       py >= draw_clip_top && py < draw_clip_bottom)))
                                pixels[py * W + px] = blend(pixels[py * W + px], color, a);
                        }
            }
        x += width * 2;
    }
    return x;
}
#endif
/* Smooth 3:2 text scaling for the monochrome UI's primary headings.
 * Bilinear alpha interpolation, drawn into the native framebuffer. */
static int heading_text(int x, int y, const char *s, u32 color) {
    while (*s) {
        int cp = may_utf8_next(&s);
        MayGlyph bits = glyph_pixels(cp, display_language);
        int src_width = glyph_width(cp);
        int out_width = src_width * 3 / 2;
        for (int py = 0; py < 30; py++) {
            int sy = py * 2 / 3, fy = (py * 2) % 3;
            int sy1 = sy + 1 < 20 ? sy + 1 : sy;
            for (int px = 0; px < out_width; px++) {
                int sx = px * 2 / 3, fx = (px * 2) % 3;
                int sx1 = sx + 1 < src_width ? sx + 1 : sx;
            int a00 = glyph_alpha(bits, sy * 16 + sx);
            int a10 = glyph_alpha(bits, sy * 16 + sx1);
            int a01 = glyph_alpha(bits, sy1 * 16 + sx);
            int a11 = glyph_alpha(bits, sy1 * 16 + sx1);
                u32 a = (u32)(a00 * (3 - fx) * (3 - fy) +
                              a10 * fx * (3 - fy) +
                              a01 * (3 - fx) * fy +
                              a11 * fx * fy) / 9;
                int dx = x + px, dy = y + py;
                if (a && dx >= 0 && dy >= 0 && dx < W && dy < H &&
                    (!draw_clip_active || (dx >= draw_clip_left && dx < draw_clip_right &&
                                           dy >= draw_clip_top && dy < draw_clip_bottom)))
                    pixels[dy * W + dx] = blend(pixels[dy * W + dx], color, a);
            }
        }
        x += out_width;
    }
    return x;
}
/* Separate proportional Sans alpha atlas; generated from installed
 * DejaVu Sans at BUILD TIME. No font engine or browser in MayKernel runtime.
 * One fixed baseline for every glyph avoids uneven letter heights. */
static int reference_font_glyph(int codepoint) {
    int lo = 0, hi = PREMIUM_FONT_COUNT - 1;
    while (lo <= hi) {
        int middle = (lo + hi) / 2;
        if (premium_font_code[middle] == codepoint) return middle;
        if (premium_font_code[middle] < codepoint) lo = middle + 1;
        else hi = middle - 1;
    }
    return -1;
}
static int reference_font_width(const char *s, int title) {
    int total = 0;
    while (*s) {
        int codepoint = may_utf8_next(&s), g = reference_font_glyph(codepoint);
        total += g >= 0 ? (title ? premium_title_advance[g] :
                                     premium_body_advance[g]) : 12;
    }
    return total;
}
static void reference_text(int x, int y, const char *s, u32 color, int title) {
    int small = title == 2, large = title == 1;
    int stride = large ? 36 : 28, height = large ? 40 : small ? 23 : 30;
    while (*s) {
        int codepoint = may_utf8_next(&s), g = reference_font_glyph(codepoint);
        if (g < 0) { x += 12; continue; }
        const u8 *bitmap = large ? premium_title_bitmap[g] : premium_body_bitmap[g];
        int advance = large ? premium_title_advance[g] : premium_body_advance[g];
        if (small) advance = (advance * 3 + 2) / 4;
        for (int yy = 0; yy < height; yy++) {
            int py = y + yy;
            if (py < 0 || py >= H ||
                (draw_clip_active && (py < draw_clip_top || py >= draw_clip_bottom))) continue;
            for (int xx = 0; xx < stride && xx < advance + 3; xx++) {
                int px = x + xx;
                if (px < 0 || px >= W ||
                    (draw_clip_active && (px < draw_clip_left || px >= draw_clip_right))) continue;
                int source_y = small ? yy * 4 / 3 : yy;
                int source_x = small ? xx * 4 / 3 : xx;
                u8 alpha = bitmap[source_y * stride + source_x];
                if (alpha)
                    pixels[py * W + px] = blend(pixels[py * W + px], color, alpha);
            }
        }
        x += advance;
    }
}
static void reference_right_label(int right, int y, const char *label, u32 color) {
    reference_text(right - reference_font_width(label, 0), y, label, color, 0);
}
static void reference_monitor_icon(int x, int y, u32 color, u32 background) {
    /* A flat outlined monitor, matching the supplied reference instead
       of an unrelated Settings gear icon. */
    soft_roundrect(x, y, 25, 20, 4, color);
    soft_roundrect(x + 2, y + 2, 21, 16, 2, background);
    rect(x + 11, y + 20, 3, 3, color);
    rect(x + 7, y + 23, 11, 2, color);
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
static void document_symbol(int x, int y, int size, u32 color);
static void symbol(int x, int y, int kind, int size, u32 c) {
#if MAYOS_MATERIAL_ICONS
    if (kind >= 0 && kind < (int)(sizeof(material_icons) / sizeof(material_icons[0])) && size > 0) {
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
        /* network / globe */
        ring(cx, cy, size / 3, thick, c);
        stroke(x + m, cy, x + size - m, cy, thick, c);
        stroke(cx, y + m, cx, y + size - m, thick, c);
    } else if (kind == 5) {
        document_symbol(x, y, size, c);
    } else if (kind == 6) {
        /* Material Symbols apps: nine individually separated circular dots. */
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++)
                circle(x + m + 2 + col * (size / 5),
                       y + m + 2 + row * (size / 5), size / 16 + 1, c);
    } else if (kind == 7) {
        /* Notifications bell, outline with clapper. */
        ring(cx, cy + 1, size / 4, thick, c);
        rect(cx - size / 4, cy + size / 12, size / 2 + 1, thick + 2,
             dark ? 0x211f23 : 0xf6f6f8);
        stroke(cx - size / 3, cy + size / 4, cx + size / 3, cy + size / 4, thick, c);
        circle(cx, cy + size / 3, thick, c);
    } else if (kind == 8 || kind == 13) {
        /* Connected and disconnected Wi-Fi states. */
        stroke(x + m, y + m + 3, cx, y + m - 2, thick - 1, c);
        stroke(cx, y + m - 2, x + size - m, y + m + 3, thick - 1, c);
        stroke(x + m + 4, cy + 1, cx, cy - 3, thick - 1, c);
        stroke(cx, cy - 3, x + size - m - 4, cy + 1, thick - 1, c);
        circle(cx, y + size - m - 2, 2, c);
        if (kind == 13)
            stroke(x + 3, y + 3, x + size - 3, y + size - 3, 2, c);
    } else if (kind == 9) {
        /* Brightness control icon. */
        ring(cx, cy, size / 5, thick, c);
        for (int i = -1; i <= 1; i += 2) {
            stroke(cx + i * size / 3, cy, cx + i * size / 4, cy, thick, c);
            stroke(cx, cy + i * size / 3, cx, cy + i * size / 4, thick, c);
        }
        stroke(cx - size / 4, cy - size / 4, cx - size / 3, cy - size / 3, 1, c);
        stroke(cx + size / 4, cy + size / 4, cx + size / 3, cy + size / 3, 1, c);
    } else if (kind == 10) {
        ring(cx, cy, size / 3, thick, c);
        stroke(cx, cy, cx, cy - size / 5, 2, c);
        stroke(cx, cy, cx + size / 5, cy + size / 6, 2, c);
    } else if (kind == 12) {
        ring(cx, cy, size / 3, thick, c);
        rect(cx - 3, cy - size / 3 - 1, 6, size / 4,
             dark ? 0x211f23 : 0xf6f6f8);
        stroke(cx, cy - size / 3 - 2, cx, cy, 2, c);
    } else if (kind == 14) {
        stroke(x + m, y + m, x + size - m, y + size - m, thick, c);
        stroke(x + m, y + size - m, x + size - m, y + m, thick, c);
    } else if (kind == 15) {
        rect(x + m, cy, size - 2 * m, thick, c);
    } else if (kind == 16 || kind == 29) {
        if (kind == 29) border(x + m + 3, y + m - 2, size - 2 * m, size - 2 * m, c);
        border(x + m, y + m, size - 2 * m, size - 2 * m, c);
    } else if (kind == 19) {
        ring(cx - 1, cy - 1, size / 4, thick, c);
        stroke(cx + 2, cy + 2, x + size - m, y + size - m, thick, c);
    } else if (kind == 23) {
        border(x + m, y + m, size - 2 * m, size / 2, c);
        rect(cx - 1, y + m + size / 2, 2, 3, c);
        rect(cx - 4, y + m + size / 2 + 3, 8, 1, c);
    } else if (kind == 24) {
        rect(x + m, cy - 2, 4, 5, c);
        stroke(x + m + 3, cy - 2, cx, y + m, 1, c);
        stroke(cx, y + m, cx, y + size - m, 1, c);
        stroke(cx, y + size - m, x + m + 3, cy + 2, 1, c);
        stroke(cx + 3, cy - 3, cx + 5, cy, 1, c);
        stroke(cx + 5, cy, cx + 3, cy + 3, 1, c);
    } else if (kind == 25) {
        ring(cx, cy, size / 3, thick, c);
        circle(cx - 3, cy - 1, 1, c);
        circle(cx, cy - 3, 1, c);
        circle(cx + 3, cy - 1, 1, c);
    } else if (kind == 26) {
        rect(cx, y + m, 2, size / 2, c);
        stroke(cx - 3, cy, cx, cy + 3, thick, c);
        stroke(cx, cy + 3, cx + 3, cy, thick, c);
        rect(x + m, y + size - m, size - 2 * m, 1, c);
    } else if (kind == 27) {
        ring(cx, cy, size / 3, thick, c);
        rect(cx, cy - 3, 1, 1, c);
        rect(cx, cy, 1, 4, c);
    } else if (kind == 28) {
        ring(cx, cy, size / 3, thick, c);
        stroke(x + m, cy, x + size - m, cy, 1, c);
        rect(cx, y + m, 1, size - 2 * m, c);
    }
}
static void icon(int x, int y, int kind, int size) {
    symbol(x + 4, y + 4, kind, size - 8, ink);
}
static void button(int x, int y, int w, const char *s, int primary) {
    if (primary) soft_roundrect(x, y, w, 30, 10, accent);
    else ui_card(x, y, w, 30, 10, surface_high);
    text(x + 12, y + 4, s, primary ? on_accent : ink);
}
enum { LANGUAGE_CELL_H = 47, LANGUAGE_ROW_H = 52, LANGUAGE_GAP = 12 };
static void language_grid(int x, int y, int width, int choice, u32 foreground, u32 secondary) {
    int cell = (width - LANGUAGE_GAP) / 2;
    for (int i = 0; i < MAY_LOCALE_COUNT; i++) {
        int xx = x + (i % 2) * (cell + LANGUAGE_GAP);
        int yy = y + (i / 2) * LANGUAGE_ROW_H;
        ui_card(xx, yy, cell, LANGUAGE_CELL_H, 11, choice == i ? selected_fill : surface_high);
        text_face(xx + 13, yy + 4, languages[i].name, foreground, (enum MayLocale)i);
        text(xx + 13, yy + 24, languages[i].tag, secondary);
        if (choice == i) {
            stroke(xx + cell - 30, yy + 22, xx + cell - 25, yy + 27, 2, foreground);
            stroke(xx + cell - 25, yy + 27, xx + cell - 16, yy + 17, 2, foreground);
        }
    }
}
static int language_hit(int px, int py, int x, int y, int width) {
    int cell = (width - LANGUAGE_GAP) / 2;
    for (int i = 0; i < MAY_LOCALE_COUNT; i++) {
        int xx = x + (i % 2) * (cell + LANGUAGE_GAP);
        int yy = y + (i / 2) * LANGUAGE_ROW_H;
        if (px >= xx && px < xx + cell && py >= yy && py < yy + LANGUAGE_CELL_H) return i;
    }
    return -1;
}
static enum MayLocale language_move(enum MayLocale choice, int key) {
    int next = choice;
    if (key == 0x101) next -= 2;       /* Up */
    else if (key == 0x102) next += 2;  /* Down */
    else if (key == MAYOS_KEY_LEFT && next % 2) next--;
    else if (key == MAYOS_KEY_RIGHT && !(next % 2)) next++;
    return next >= 0 && next < MAY_LOCALE_COUNT ? (enum MayLocale)next : choice;
}
typedef struct {
    int x, y, w, h, open, min, max, ox, oy, ow, oh;
    const char *title;
} Window;
#ifdef MAYOS_TASK_MANAGER
#define TASK_WINDOW 5
#define WINDOW_COUNT 6
#else
#define WINDOW_COUNT 5
#endif
/* Nonblocking 180ms fades. Geometry and input targets never move. */
static u32 animation_started[WINDOW_COUNT];
static int animation_running[WINDOW_COUNT];
static u32 animation_underlay[W * H];
enum SettingsTab {
    SETTINGS_GENERAL, SETTINGS_DISPLAY, SETTINGS_SOUND, SETTINGS_NETWORK,
    SETTINGS_PERSONAL, SETTINGS_UPDATES, SETTINGS_ABOUT, SETTINGS_LANGUAGE,
    SETTINGS_CREDITS, SETTINGS_TAB_COUNT
};
enum DesktopMenu {
    MENU_MAYOS, MENU_APP, MENU_FILE, MENU_EDIT, MENU_GO, MENU_SECTIONS,
    MENU_INPUT, MENU_SHELL, MENU_NAVIGATE, MENU_VIEW, MENU_WINDOW, MENU_HELP, MENU_COUNT
};

#define DOCK_TOP (H - 80)
#define DOCK_HEIGHT 70
enum DockItem {
    DOCK_LAUNCHER, DOCK_FILES, DOCK_SETTINGS, DOCK_TERMINAL,
    DOCK_EDITOR, DOCK_EXPLORER, DOCK_TASK, DOCK_TRASH, DOCK_FIREFOX
};
#define DOCK_SLOT_COUNT (WINDOW_COUNT + 3)

static int dock_level[DOCK_FIREFOX + 1], dock_popup = -1;
static int dock_layout(int items[DOCK_SLOT_COUNT]);
static int dock_hit(int x, int y);
static int dock_slot_x(int slot, int count) {
    return (W - (16 + count * 57 + 14)) / 2 + 9 + slot * 57 +
           (slot == count - 1 ? 14 : 0);
}
#define WINDOW_CONTROL_SIZE 33
#define WINDOW_CONTROL_GAP 3
#define WINDOW_CONTROL_TOP 5
#define WINDOW_TITLE_HEIGHT 36
static u32 animation_frame;
static void animate_window(int id) {
    animation_started[id] = ticks;
    animation_running[id] = !high_contrast;
}
static int animation_step(int mx, int my) {
    u32 now = ticks;
    if ((u32)(now - animation_frame) < 2) return 0;
    animation_frame = now;
    int changed = 0;
    for (int i = 0; i < WINDOW_COUNT; i++)
        if (animation_running[i]) changed = 1;
    int hovered = dock_hit(mx, my);
    for (int i = 0; i <= DOCK_FIREFOX; i++) {
        int target = !high_contrast && i == hovered ? 256 : 0;
        int delta = target - dock_level[i];
        if (delta) {
            int step = delta / 3;
            if (!step) step = delta;
            dock_level[i] += step;
            changed = 1;
        }
    }
    return changed;
}
static Window windows[WINDOW_COUNT] = {{275, 105, 730, 510, 0, 0, 0, 0, 0, 0, 0, "Files"},
                            {228, 128, 824, 436, 0, 0, 0, 0, 0, 0, 0, "Settings"},
                            {290, 136, 700, 448, 0, 0, 0, 0, 0, 0, 0, "终端 / Terminal"},
                            {315, 112, 650, 490, 0, 0, 0, 0, 0, 0, 0, "文本编辑器 / Editor"},
                            {240, 100, 800, 520, 0, 0, 0, 0, 0, 0, 0, "MayOS Explorer (Preview)"},
#ifdef MAYOS_TASK_MANAGER
                            {62, 56, 1156, 570, 0, 0, 0, 0, 0, 0, 0, "MayOS Task Manager"},
#endif
};
static int order[WINDOW_COUNT] = {0, 2, 3, 4,
#ifdef MAYOS_TASK_MANAGER
                                 TASK_WINDOW,
#endif
                                 1}, focus = -1, dirty = 1;
static int mx = 640, my = 360, buttons, oldbuttons, drag = -1, dragx, dragy;
static int start_open = 0, context_open = 0, ctxx, ctxy, folder = 1, selected = -1, clip = -1,
           cut = 0, settings_tab = SETTINGS_ABOUT;
static int active_menu = -1, menu_row = -1;
static int menu_left[MENU_COUNT], menu_right[MENU_COUNT];
static int settings_search_active = 0;
static u32 display_trial_deadline;
static unsigned display_trial_seconds;

static void display_trial_finish(int keep) {
    if (!display_trial_deadline) return;
    if (!platform_display_finish(keep)) return;
    display_trial_deadline = 0;
    dirty = 1;
}

static int sound_level = -1;
static char settings_search[36];
static int contains(const char *a, const char *b);
static const char *pointer_label(const char *zh, const char *en);
static int file_scroll = 0;
static int desktop_note_selected = 0;
static int top_panel = 0;  /* 1=clock, 2=network, 3=display, 4=alerts, 5=power */
#define TOP_STATUS_LEFT 964
#define TOP_STATUS_STEP 34
#define STATUS_PANEL_X (W - 336)
static int modal = 0, modal_target = -1;
enum EasterEgg { EGG_CLOSED, EGG_CAT, EGG_TENSHI };
static enum EasterEgg egg_open;
static int egg_clicks;
static u32 egg_last_click;
static void open_easter(enum EasterEgg artwork) {
    egg_open = artwork;
    egg_clicks = 0;
    start_open = context_open = modal = 0;
    active_menu = -1;
    drag = -1;
    dirty = 1;
    serial(artwork == EGG_CAT ? "MayOS: meow easter egg opened\n"
                             : "MayOS: angelkawaii easter egg opened\n");
}
enum { MODAL_POWER = 7 };
enum PowerAction { POWER_RESTART, POWER_SHUTDOWN };
enum PowerRisk { POWER_EDITOR_CHANGES, POWER_SESSION_FILES, POWER_WRITE_FAILED };
static enum PowerRisk power_risk;
static int workspace_pending;
static char modal_text[64], notice[120], search[48], device[48] = "MayOS Workstation";
static u32 notice_until = 0, last_click_tick;
static int last_clicked = -1;
static int editor_file = -1, editor_changed = 0;
static void notify(const char *s);
static char editor_buf[FILE_CAP], editor_clipboard[FILE_CAP];
static int editor_caret, editor_anchor, editor_scroll_row, editor_dragging;
#define EDITOR_HISTORY 12
typedef struct { char text[FILE_CAP]; int caret, anchor; } EditorRevision;
static EditorRevision editor_undo[EDITOR_HISTORY], editor_redo[EDITOR_HISTORY];
static int editor_undo_count, editor_redo_count;

static void editor_reset_view(void) {
    editor_anchor = editor_caret = strlen(editor_buf);
    editor_scroll_row = editor_dragging = editor_undo_count = editor_redo_count = 0;
}

static void editor_check_dirty(void) {
    editor_changed = strcmp(editor_buf, (char *)files[editor_file].data) != 0;
}

static void editor_remember(EditorRevision *stack, int *count) {
    if (*count == EDITOR_HISTORY) {
        for (int i = 1; i < *count; i++) stack[i - 1] = stack[i];
        (*count)--;
    }
    EditorRevision *saved = &stack[(*count)++];
    copy(saved->text, editor_buf, sizeof(saved->text));
    saved->caret = editor_caret;
    saved->anchor = editor_anchor;
}

static void editor_recover(int redo) {
    EditorRevision *source = redo ? editor_redo : editor_undo;
    int *count = redo ? &editor_redo_count : &editor_undo_count;
    if (!*count) return;
    editor_remember(redo ? editor_undo : editor_redo, redo ? &editor_undo_count : &editor_redo_count);
    const EditorRevision *saved = &source[--*count];
    copy(editor_buf, saved->text, sizeof(editor_buf));
    editor_caret = saved->caret;
    editor_anchor = saved->anchor;
    editor_check_dirty();
}

static int editor_replace(int begin, int end, const char *value, int size) {
    int old_size = strlen(editor_buf), delta = size - (end - begin);
    if ((u32)(old_size + delta) > fs_file_limit()) {
        notify("文稿已达到容量上限 / Document size limit reached");
        return 0;
    }
    if (begin == end && !size) return 1;
    editor_remember(editor_undo, &editor_undo_count);
    editor_redo_count = 0;
    if (delta > 0)
        for (int i = old_size; i >= end; i--) editor_buf[i + delta] = editor_buf[i];
    else if (delta < 0)
        for (int i = end; i <= old_size; i++) editor_buf[i + delta] = editor_buf[i];
    memcpy(editor_buf + begin, value, size);
    editor_anchor = editor_caret = begin + size;
    editor_check_dirty();
    return 1;
}

static int editor_insert(const char *value, int size) {
    int begin = editor_caret < editor_anchor ? editor_caret : editor_anchor;
    int end = editor_caret > editor_anchor ? editor_caret : editor_anchor;
    return editor_replace(begin, end, value, size);
}
static MayIme editor_ime, modal_ime;
static char output[180][100], command[160], history[160];
static int out_count, cmdlen, cwd = 0, scroll = 0;
static void notify(const char *s) {
    copy(notice, s, sizeof(notice));
    notice_until = ticks + 350;
    dirty = 1;
}
static const char *window_menu_name(int id) {
    switch (id) {
    case 0: return L(FILES);
    case 1: return L(SETTINGS);
    case 2: return L(TERMINAL);
    case 3: return L(EDITOR);
    case 4: return "MayOS Explorer";
    default: return id < 0 ? pointer_label("桌面", "Desktop") : windows[id].title;
    }
}

static void set_focus(int id) {
    if (focus == id) return;
    static const char *const names[] = {"files", "settings", "terminal", "editor", "explorer"
#ifdef MAYOS_TASK_MANAGER
        , "task-manager"
#endif
    };
    focus = id;
    active_menu = menu_row = -1;
    settings_search_active = 0;
    serial("MayOS: active application menu: ");
    serial(id < 0 ? "desktop" : names[id]);
    serial("\n");
}

static void focus_visible_window(void) {
    for (int i = WINDOW_COUNT - 1; i >= 0; i--) {
        int id = order[i];
        if (windows[id].open && !windows[id].min) {
            set_focus(id);
            return;
        }
    }
    set_focus(-1);
}

static void minimize_window(int id) {
    windows[id].min = 1;
    if (id == focus) focus_visible_window();
}

static void raise_window(int id) {
    if (id == 1 && !windows[id].open) {
        language_choice = display_language;
        language_save_failed = 0;
        settings_search[0] = 0;
    }
    if (!windows[id].open || windows[id].min) animate_window(id);
    int at = 0;
    for (; at < WINDOW_COUNT && order[at] != id; at++)
        ;
    for (int i = at; i < WINDOW_COUNT - 1; i++)
        order[i] = order[i + 1];
    order[WINDOW_COUNT - 1] = id;
    windows[id].open = 1;
    windows[id].min = 0;
    set_focus(id);
    start_open = context_open = 0;
    active_menu = -1;
    if (id != 1) settings_search_active = 0;
    dirty = 1;
}
#ifdef MAYOS_TASK_MANAGER
static void task_manager_open(void) {
#ifdef MAYOS_LINUX
    if (!windows[TASK_WINDOW].open || windows[TASK_WINDOW].min) may_task_cancel();
#endif
    raise_window(TASK_WINDOW);
    serial("MayOS: Task Manager opened\n");
}
#endif
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
static MayVfsReader vfs_reader;
static int read_text_file(int node, char *buffer) {
    if (!may_vfs_open(node, &vfs_reader)) return 0;
    u32 length = may_vfs_read(&vfs_reader, (u8 *)buffer, FILE_CAP - 1);
    buffer[length] = 0;
    return 1;
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
        *parent = may_vfs_lookup(prefix, cwd);
    }
    if (strlen(p + split + 1) >= 48) return 0;
    copy(name, p + split + 1, 48);
    MayVfsNode node;
    return may_vfs_info(*parent, &node) && node.directory && name[0];
}
static void pwd(void) {
    char path[180] = "/";
    const char *chain[FS_MAX];
    int n = 0, a = cwd;
    MayVfsNode node;
    while (a && n < FS_MAX && may_vfs_info(a, &node)) {
        chain[n++] = node.name;
        a = node.parent;
    }
    while (n) {
        copy(path + strlen(path), chain[--n], 180 - strlen(path));
        if (n)
            copy(path + strlen(path), "/", 180 - strlen(path));
    }
    emit(path);
}
static int sync_workspace(void) {
    int ok = fs_sync();
    if (ok) workspace_pending = 0;
    return ok;
}

static int sync_changes(void) {
    workspace_pending = 1;
    return sync_workspace();
}

static int sync_notice(void) {
    int ok = sync_changes();
    if (!ok) {
        notify(disk_ready ?
            pointer_label("写入磁盘失败，修改仍在内存中", "Disk write failed. Changes are still in memory.") :
            pointer_label("修改仅保存在当前会话", "Changes are saved for this session only."));
    }
    return ok;
}
static void begin_modal(int kind, int target);

static void run_power_action(enum PowerAction action) {
    if (action == POWER_RESTART) reboot();
    else poweroff();
}

#ifdef MAYOS_LINUX
static int media_state, media_percent;
#endif
static void request_power_action(enum PowerAction action) {
#ifdef MAYOS_LINUX
    if (media_state == 3) {
        notify(pointer_label("正在安装更新，请稍候", "Wait for update installation to finish."));
        return;
    }
#endif
    if (editor_changed || editor_ime.size) {
        power_risk = POWER_EDITOR_CHANGES;
    } else if (!workspace_pending || (disk_ready && sync_workspace())) {
        run_power_action(action);
        return;
    } else {
        power_risk = disk_ready ? POWER_WRITE_FAILED : POWER_SESSION_FILES;
    }
    begin_modal(MODAL_POWER, action);
    serial("MayOS: power action awaiting confirmation\n");
}

static void shell(void) {
    char full[200];
    copy(full, profile_name, sizeof(full));
    copy(full + strlen(full), "@mayos:~ $ ", sizeof(full) - strlen(full));
    copy(full + strlen(full), command, sizeof(full) - strlen(full));
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
    int target = argc > 1 ? may_vfs_lookup(argv[1], cwd) : -1;
    MayVfsNode info;
    if (!strcmp(c, "help")) {
        emit("help  fastfetch  clear  pwd  ls [path]  cd <path>");
        emit("cat <file>  mkdir <path>  touch <path>  write <file> <text>");
        emit("cp <path> <folder>  mv <path> <new-name>  rm <path>  save");
        emit("theme light|dark  net status|dhcp|resolve DOMAIN|tcp-echo|qemu-probe|udp-probe|dns-probe|tcp-probe|tcp-loss-probe");
        emit("tls status|check <host> (native MayKernel HTTPS security check)");
        emit("hardware  storage  lspci  reboot  shutdown");
#ifdef MAYOS_TASK_MANAGER
        emit("taskmgr  (F2 opens MayOS Task Manager)");
#endif
        emit("F1 opens Terminal. F3 cycles input methods in the editor and name dialogs.");
#ifdef MAYOS_TASK_MANAGER
    } else if (!strcmp(c, "taskmgr")) {
        task_manager_open();
#endif
    } else if (!strcmp(c, "clear"))
        out_count = 0;
    else if (!strcmp(c, "pwd"))
        pwd();
    else if (!strcmp(c, "ls")) {
        int d = argc > 1 ? target : cwd;
        if (!may_vfs_info(d, &info) || !info.directory)
            emit("ls: directory not found");
        else {
            for (int i = may_vfs_next(d, -1); i >= 0; i = may_vfs_next(d, i)) {
                if (may_vfs_info(i, &info)) {
                    char b[100];
                    copy(b, info.directory ? "[dir]  " : "       ", 100);
                    copy(b + 7, info.name, 93);
                    emit(b);
                }
            }
        }
    } else if (!strcmp(c, "cp")) {
        int destination = argc == 3 ? may_vfs_lookup(argv[2], cwd) : -1;
        if (fs_copy_tree(target, destination) < 0)
            emit("cp: conflict, capacity, or invalid destination");
        else {
            sync_notice();
            emit("Copied");
        }
    } else if (!strcmp(c, "cd")) {
        if (argc < 2)
            cwd = 0;
        else if (may_vfs_info(target, &info) && info.directory)
            cwd = target;
        else
            emit("cd: directory not found");
    } else if (!strcmp(c, "cat")) {
        static char contents[FILE_CAP];
        if (read_text_file(target, contents))
            emit(contents);
        else
            emit("cat: file not found");
    } else if (!strcmp(c, "mkdir") || !strcmp(c, "touch") || !strcmp(c, "write")) {
        int par;
        char name[48];
        if (argc < 2 || (!strcmp(c, "write") && argc < 3) || !path_parent(argv[1], &par, name)) {
            emit("usage: mkdir/touch <path> | write <file> <text>");
        } else if (may_vfs_read_only(par) || may_vfs_read_only(target)) {
            emit("Read-only filesystem");
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
                    emit(sync_notice() ? "OK" : disk_ready ?
                         "OK in memory (disk write failed)" : "OK (RAM only; disk sync unavailable)");
                }
            }
        }
    } else if (!strcmp(c, "rm")) {
        if (may_vfs_read_only(target))
            emit("Read-only filesystem");
        else if (target < 4)
            emit("rm: missing path or protected system folder");
        else if (!fs_move(target, 3))
            emit("rm: cannot move to Trash (name already exists?)");
        else {
            sync_notice();
            emit("Moved to Trash");
        }
    } else if (!strcmp(c, "mv")) {
        if (may_vfs_read_only(target))
            emit("Read-only filesystem");
        else if (argc < 3 || !fs_rename(target, argv[2]))
            emit("mv: invalid or duplicate name");
        else {
            sync_notice();
            emit("Renamed");
        }
    } else if (!strcmp(c, "save")) {
#ifdef MAYOS_LINUX
        emit(sync_workspace() ? "MayOS Linux: persistent desktop snapshot committed" :
                         "MayOS Linux: RAM session or profile write unavailable");
#else
        emit(sync_workspace() ? "MayFS: synchronized to disk" : "MayFS: disk unavailable");
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
        emit("Kernel: MayKernel x86_64 (custom)");
        emit("Desktop canvas: 1280 x 720 / native 16:9");
        emitnum("Memory: ", memory_mb, " MiB");
        emitnum("Uptime: ", ticks / 100, " seconds");
        emit(disk_ready ? "Disk: ATA / MayFS" : "Disk: RAM fallback");
        emit(cpu_name);
#endif
    } else if (!strcmp(c, "update")) {
#ifndef MAYOS_LINUX
        if (argc > 1 && !strcmp(argv[1], "check"))
            may_update_check();
        else if (argc > 1 && !strcmp(argv[1], "verify"))
            may_update_verify_local();
        else if (argc > 1 && !strcmp(argv[1], "auth"))
            may_update_authenticate_local();
        else if (argc > 1 && !strcmp(argv[1], "auth-stage"))
            may_update_authenticate_staged();
        else if (argc > 1 && !strcmp(argv[1], "auth-stream-stage-test")) {
#ifdef MAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC
            int passed=may_update_signed_ata_stream_diagnostic();
            emit(passed ? "Signed stream-to-ATA QEMU diagnostic PASS (NO INSTALL)"
                        : "Signed stream-to-ATA QEMU diagnostic REJECTED (NO INSTALL)");
            emit(may_update_signed_ata_stream_status());
#else
            emit("Signed ATA streaming: disabled outside isolated QEMU diagnostic");
#endif
        }
        else if (argc > 1 && !strcmp(argv[1], "auth-stream-test")) {
#ifdef MAYOS_SIGNED_STREAM_DIAGNOSTIC
            int passed=may_update_signed_stream_diagnostic();
            emit(passed ? "Signed 64KiB HTTP stream diagnostic PASS (NO DOWNLOAD)"
                        : "Signed 64KiB HTTP stream diagnostic REJECTED (NO DOWNLOAD)");
            emit(may_update_signed_stream_status());
#else
            emit("Signed stream: disabled outside isolated QEMU diagnostic");
#endif
        }
        else if (argc > 1 &&
            (!strcmp(argv[1], "stage-begin") ||
             !strcmp(argv[1], "stage-half") ||
             !strcmp(argv[1], "stage-rest") ||
             !strcmp(argv[1], "stage-verify") ||
             !strcmp(argv[1], "stage-status"))) {
#ifdef MAYOS_STAGE_DISK_DIAGNOSTIC
            int result=0;
            if(!strcmp(argv[1],"stage-begin"))
                result=may_stage_diag_fixture_begin();
            else if(!strcmp(argv[1],"stage-half"))
                result=may_stage_diag_fixture_append(8u);
            else if(!strcmp(argv[1],"stage-rest"))
                result=may_stage_diag_fixture_append(16u);
            else if(!strcmp(argv[1],"stage-verify"))
                result=may_stage_diag_verify();
            else result=may_stage_diag_status();
            emit(result ? "QEMU large-image staging diagnostic PASS (NO INSTALL)"
                        : "QEMU large-image staging diagnostic REJECTED/FAILED");
#else
            emit("Large-image streaming: production path disabled (no secure transport)");
#endif
        } else if (argc > 1 &&
            (!strcmp(argv[1], "journal-status") ||
             !strcmp(argv[1], "journal-init") ||
             !strcmp(argv[1], "journal-stage"))) {
#ifdef MAYOS_AB_DISK_DIAGNOSTIC
            int mode=!strcmp(argv[1],"journal-init") ? 1 :
                     !strcmp(argv[1],"journal-stage") ? 2 : 0;
            int result=may_ab_journal_diagnostic(mode);
            emit(result ? "QEMU A/B journal diagnostic PASS (no installation)"
                        : "QEMU A/B journal diagnostic REFUSED/FAILED");
#else
            emit("A/B persistent journal: disabled outside opt-in QEMU test build");
#endif
        } else if (argc > 1 && !strcmp(argv[1], "recovery-test")) {
#ifdef MAYOS_RUST_CORE
            int passed=mayrs_ab_selftest();
            emit(passed ? "A/B boot policy PASS (memory only; no installation)"
                        : "A/B boot policy FAIL (memory only; no installation)");
            serial(passed ? "MayOS: Rust A/B recovery policy self-test PASSED\n"
                          : "MayOS: Rust A/B recovery policy self-test FAILED\n");
#else
            emit("A/B boot policy: requires optional Rust kernel build");
#endif
        } else if (argc > 1 && strcmp(argv[1], "status")) {
            emit("Usage: update check|verify|auth|auth-stage|auth-stream-test|auth-stream-stage-test|recovery-test|stage-begin|stage-half|stage-rest|stage-verify|stage-status|status");
            return;
        }
        emit(may_update_status());
        emit(may_update_verify_status());
        emit(may_update_auth_status());
        emit(may_update_auth_staged_status());
        const MayUpdateInfo *info = may_update_info();
        if (info->state == MAY_UPDATE_CANDIDATE ||
            info->state == MAY_UPDATE_CURRENT) {
            emit(info->name);
            emit(info->version);
            emit(info->architecture);
            emit(info->kernel);
            emit(info->release_date);
            emit(info->changelog);
            emit("SHA-256 in manifest is NOT an authenticated image hash.");
        }
        emit("HTTPS remains BLOCKED: native trusted TLS not implemented.");
        emit("Ed25519 auth needs owner-pinned key + .sig + matching image.");
        emit("No download/install; rollback-capable installer not ready.");
#else
        emit("Native MayKernel update check requires the MayKernel boot option.");
#endif
    } else if (!strcmp(c, "tls")) {
#ifdef MAYOS_LINUX
        emit("TLS preflight: MayKernel-native only");
#else
        const MayTlsSecurityReport *state=may_tls_security_report();
        if (argc > 1 && !strcmp(argv[1], "time-challenge")) {
            char challenge[65];
            if (may_time_challenge(challenge)) {
                emit("MayOS UTC challenge:");
                emit(challenge);
            } else emit("Trusted time enrollment unavailable (authority key or RDSEED missing)");
            return;
        }
        if (argc == 3 && !strcmp(argv[1], "time-begin")) {
            emit(may_time_begin(argv[2]) ? "Trusted time epoch staged" : "Trusted time epoch rejected");
            return;
        }
        if (argc == 3 && !strcmp(argv[1], "time-part")) {
            emit(may_time_part(argv[2]) ? "Trusted time signature fragment staged" : "Trusted time signature fragment rejected");
            return;
        }
        if (argc == 2 && !strcmp(argv[1], "time-apply")) {
            emit(may_time_apply() ? "Trusted UTC accepted for this boot" : "Trusted UTC signature rejected");
            return;
        }

#ifdef MAYOS_TLS_WIRE_DIAGNOSTIC
        if(argc == 4 && !strcmp(argv[1], "wire-test")) {
            emit(may_tls_wire_test(argv[2], argv[3]) ? "TLS wire diagnostic started" :
                 "TLS wire diagnostic unavailable");
            return;
        }
#endif
        if(argc>1 && !strcmp(argv[1],"x509-test")) {
#ifdef MAYOS_TLS_X509_DIAGNOSTIC
            int passed=may_tls_x509_fixture_selftest();
            emit(passed
                ? "BearSSL X.509 test CA and hostname PASS (NO TLS HANDSHAKE)"
                : "BearSSL X.509 test CA and hostname FAIL (NO TLS HANDSHAKE)");
#else
            emit("BearSSL X.509 test CA: not linked in production build");
#endif
        } else if(argc>1 && !strcmp(argv[1],"digest-test")) {
#ifdef MAYOS_RUST_CORE
            int passed=may_update_stream_selftest();
            emit(passed ? "HTTP stream SHA-256 and exact-length PASS (NO INSTALL)"
                        : "HTTP stream SHA-256 and exact-length FAIL (NO INSTALL)");
            serial(passed ? "MayOS: HTTP stream digest self-test PASSED (NO DOWNLOAD)\n"
                          : "MayOS: HTTP stream digest self-test FAILED (NO DOWNLOAD)\n");
#else
            emit("HTTP stream SHA-256 test requires optional Rust kernel build");
#endif
        } else if(argc>1 && !strcmp(argv[1],"body-test")) {
#ifdef MAYOS_RUST_CORE
            int passed=mayrs_http_body_selftest();
            emit(passed ? "HTTP body streaming budget PASS (NO DOWNLOAD)"
                        : "HTTP body streaming budget FAIL (NO DOWNLOAD)");
            serial(passed ? "MayOS: strict HTTP body budget self-test PASSED (NO CONNECT)\n"
                          : "MayOS: strict HTTP body budget self-test FAILED (NO CONNECT)\n");
#else
            emit("HTTP body budget needs optional Rust kernel build");
#endif
        } else if(argc>1 && !strcmp(argv[1],"http-test")) {
#ifdef MAYOS_RUST_CORE
            int passed=mayrs_http_headers_selftest();
            emit(passed ? "HTTP/1.1 update response preflight PASS (NO CONNECT)"
                        : "HTTP/1.1 update response preflight FAIL (NO CONNECT)");
            serial(passed ? "MayOS: HTTP framing strict no_std self-test PASSED (NO CONNECT)\n"
                          : "MayOS: HTTP framing strict no_std self-test FAILED (NO CONNECT)\n");
#else
            emit("HTTP update framing requires optional Rust kernel build");
#endif
        } else if(argc>1 && !strcmp(argv[1],"url-test")) {
#ifdef MAYOS_RUST_CORE
            int ok=mayrs_release_url_selftest();
            emit(ok ? "GitHub Release URL policy PASS (offline; NO HTTPS)"
                    : "GitHub Release URL policy FAILED (NO HTTPS)");
            serial(ok
                ? "MayOS: GitHub Release URL policy self-test PASSED (NO CONNECT)\n"
                : "MayOS: GitHub Release URL policy self-test FAILED (NO CONNECT)\n");
#else
            emit("GitHub Release URL policy needs optional Rust kernel build");
#endif
        } else if(argc>1 && !strcmp(argv[1],"check")) {
            if(argc<3)emit("Usage: tls check api.github.com");
            else {
                int reason=may_tls_update_preflight(argv[2]);
                emit(may_tls_block_reason(reason));
                serial("MayOS: TLS preflight fail-closed (no HTTPS request sent)\n");
            }
        } else if(argc>1 && strcmp(argv[1],"status")) {
            emit("Usage: tls status|check <host>|url-test|x509-test|http-test|body-test|digest-test");
        }
        emit(state->trusted_time_ready && state->certificate_roots_ready && state->cryptographic_drbg_ready
            ? "Native Bing HTTPS prerequisites: ready"
            : "Native Bing HTTPS prerequisites: pending");
        emit(may_tls_provider_is_linked()
            ? "BearSSL TLS engine: linked"
            : "BearSSL TLS engine: not linked in this build");
        emit(state->hardware_entropy_observed
            ? "CPU RDRAND: sampled (NOT a cryptographic DRBG)"
            : "CPU RDRAND: unavailable or health check failed");
        emit(state->rtc_observed
            ? "CMOS clock: observed but UNTRUSTED"
            : "CMOS clock: unavailable or invalid");
        emit(state->cryptographic_drbg_ready
            ? "BearSSL HMAC-DRBG: RDSEED ready"
            : "BearSSL HMAC-DRBG: RDSEED unavailable");
        emit(state->trusted_time_ready ? "Trusted TLS time: signed UTC active" :
#ifdef MAYOS_ROUGHTIME
            "Trusted TLS time: awaiting a signed network clock");
#else
            may_time_available() ? "Trusted TLS time: awaiting a signed UTC credential" :
            "Trusted TLS time: no authority configured");
#endif
        emit(state->certificate_roots_ready ? "Trusted CA roots: Mozilla installed" : "Trusted CA roots: not installed");
        emit(state->x509_engine_ready ? "X.509 chain and hostname verifier: available" : "X.509 verifier unavailable");
        emit("System update downloads are disabled");
#endif
    } else if (!strcmp(c, "storage")) {
#ifdef MAYOS_LINUX
        emit(disk_ready
             ? "Linux desktop storage: marked ext4 A/B persistence enabled"
             : "Linux desktop storage: RAM only; files disappear after restart");
#else
        emit(disk_ready ? "MayKernel: writable MayFS" : "MayKernel: RAM fallback");
#endif
    } else if (!strcmp(c, "hardware")) {
#ifdef MAYOS_LINUX
        emit("Linux desktop: MayKernel hardware diagnostics unavailable");
#else
        MayDisplayInfo display;
        display_get_info(&display);
        char width[12], height[12], message[80] = "Firmware framebuffer: ";
        number(width, display.width);
        number(height, display.height);
        copy(message + strlen(message), width, sizeof(message) - strlen(message));
        copy(message + strlen(message), "x", sizeof(message) - strlen(message));
        copy(message + strlen(message), height, sizeof(message) - strlen(message));
        emit(message);
        emitnum("xHCI controllers: ", (u32)xhci_controller_count(), "");
        emitnum("USB boot keyboards: ", (u32)xhci_boot_input_count(1), "");
        emitnum("USB boot mice: ", (u32)xhci_boot_input_count(2), "");
        emitnum("NVMe namespaces: ", nvme_namespace_count(), "");
        if (platform_ram_only()) emit("Storage: RAM-only hardware trial");
        else emit(disk_ready ? "Storage: writable MayFS" : "Storage: RAM fallback");
#endif
    } else if (!strcmp(c, "lspci")) {
#ifdef MAYOS_LINUX
        emit("lspci: use the Linux pciutils package when included in the image");
#else
        emitnum("PCI devices: ", (u32)pci_device_count(), "");
        for (int i = 0; i < pci_device_count(); i++) {
            char description[96];
            pci_describe(pci_device_at(i), description, sizeof(description));
            emit(description);
        }
#endif
    } else if (!strcmp(c, "net") || !strcmp(c, "network")) {
#ifdef MAYOS_LINUX
        char report[384];
        platform_network_status(report, sizeof(report));
        emit(report);
        emit("Explorer: HTTP(S), certificate-verified TLS, HTTP/2 and IPv6 (Linux).");
#else
        if (argc > 1 && !strcmp(argv[1], "tcp-echo")) {
            if (argc != 4 || !may_stream_echo_check(argv[2], argv[3]))
                emit("Usage: net tcp-echo <IPv4> <port> (requires DHCP and an idle TCP client)");
            else emit("MayOS TCP echo check started");
        } else if (argc > 1 && !strcmp(argv[1], "resolve")) {
            if(argc<3)
                emit("Usage: net resolve <hostname>");
            else if(!may_net_dns_lookup(argv[2]))
                emit("DNS: obtain DHCP lease first or invalid hostname/busy NIC");
            else
                emit("DNS: resolving via the DHCP-provided resolver");
        } else if (argc > 1 && !strcmp(argv[1], "dhcp")) {
            if (!may_net_dhcp_start())
                emit("DHCP: no active Ethernet link");
            else
                emit("DHCP: automatic address acquisition is enabled");
        } else if (argc > 1 && !strcmp(argv[1], "qemu-probe")) {
            if (!may_net_qemu_arp_probe())
                emit("QEMU ARP probe: NIC unavailable or transmit busy");
        } else if (argc > 1 && !strcmp(argv[1], "udp-probe")) {
            if (!may_net_qemu_udp_probe())
                emit("UDP probe: send ARP probe first; adapter may be busy");
        } else if (argc > 1 && !strcmp(argv[1], "tcp-loss-probe")) {
            if (!may_net_qemu_tcp_loss_probe())
                emit("TCP loss test: existing session busy or no ARP");
        } else if (argc > 1 && !strcmp(argv[1], "tcp-probe")) {
            if (!may_net_qemu_tcp_http_probe())
                emit("TCP QEMU test: send ARP probe first; NIC may be busy");
        } else if (argc > 1 && !strcmp(argv[1], "dns-probe")) {
            if (!may_net_qemu_dns_fixture())
                emit("DNS fixture: send ARP probe first; adapter may be busy");
        } else if (argc > 1 && strcmp(argv[1], "status"))
            emit("Usage: net status|dhcp|resolve DOMAIN|tcp-echo|qemu-probe|udp-probe|dns-probe|tcp-probe|tcp-loss-probe");
        may_net_poll();
        const MayNetInfo *nic = may_net_info();
        if (nic->present) {
            emit(nic->io_base ? "Network: RTL8139 PCI Ethernet ready (polled, L2 only)."
                              : "Network: E1000/E1000e PCI Ethernet ready.");
            emit("IPv4/UDP: QEMU-only driver, DHCP and untrusted DNS A support.");
            emitnum("RX frames: ", nic->packets_rx, "");
            emitnum("TX frames: ", nic->packets_tx, "");
            emit(nic->gateway_arp_reply
                ? "QEMU gateway ARP: reply received"
                : "QEMU gateway ARP: not confirmed");
            emit(nic->ipv4_udp_reply
                ? "QEMU IPv4 UDP echo: validated"
                : "QEMU IPv4 UDP echo: not confirmed");
            emit(nic->dns_fixture_reply
                ? "QEMU synthetic DNS A response: validated"
                : "QEMU synthetic DNS A response: not confirmed");
            if(nic->dns_native_reply) {
                emit("DNS (DHCP resolver): A answer received");
                emit(may_net_format_ipv4(nic->dns_native_address));
            } else if(nic->dns_native_timeout)
                emit("DNS (DHCP resolver): query failed or timed out");
            else
                emit("DNS (DHCP resolver): no valid answer yet");
            emit(nic->tcp_fixture_connected
                ? "QEMU TCP: 3-way handshake complete"
                : "QEMU TCP: not connected");
            emit(nic->tcp_fixture_reply
                ? "QEMU HTTP: plaintext fixture response validated"
                : "QEMU HTTP: no validated test response");
            emit(nic->tcp_fixture_closed
                ? "QEMU TCP: clean FIN close confirmed"
                : "QEMU TCP: close not confirmed");
            emitnum("QEMU TCP retransmissions: ",nic->tcp_fixture_retries,"");
            if(nic->tcp_fixture_timeout)
                emit("QEMU TCP: connection timed out / aborted");
            const MayDhcpClient *lease=may_net_dhcp_info();
            if (lease->state==MAY_DHCP_BOUND) {
                emit("DHCP: bound");
                emit("Assigned IPv4:");
                emit(may_net_format_ipv4(lease->address));
                emit("Subnet:");
                emit(may_net_format_ipv4(lease->subnet));
                emit("Gateway:");
                emit(may_net_format_ipv4(lease->router));
                emit("DNS:");
                emit(may_net_format_ipv4(lease->dns));
                emitnum("Lease (seconds): ",lease->lease_seconds,"");
            } else if (lease->state==MAY_DHCP_REQUEST)
                emit("DHCP: REQUEST sent, waiting for ACK");
            else if (lease->state==MAY_DHCP_DISCOVER)
                emit("DHCP: DISCOVER sent, waiting for OFFER");
            else if (lease->state==MAY_DHCP_NAK)
                emit("DHCP: rejected by server");
        } else
            emit("Network: no supported PCI Ethernet adapter.");
        const char *stream_states[] = {"TCP stream: idle", "TCP stream: connecting",
            "TCP stream: connected", "TCP stream: remote closed",
            "TCP stream: closed", "TCP stream: failed"};
        emit(stream_states[may_stream_status()]);
        emit("MayKernel: native TCP preview. net tcp-echo <IPv4> <port>.");
        emit("QEMU fixture UDP/DNS probes are separate from real DNS.");
#endif
    } else if (!strcmp(c, "memstress")) {
#ifndef MAYOS_LINUX
        emit(page_stress_test() ? "memstress: PASS" : "memstress: FAIL");
#else
        emit("memstress: native MayKernel only");
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
        open_easter(EGG_CAT);
    } else if (!strcmp(c, "angelkawaii") && argc == 1) {
        open_easter(EGG_TENSHI);
    } else if (!strcmp(c, "theme") && argc > 1) {
        if (!strcmp(argv[1], "light") || !strcmp(argv[1], "dark")) {
            dark = !strcmp(argv[1], "dark");
            preferences_save();
        } else emit("Usage: theme light|dark");
    } else if (!strcmp(c, "reboot")) {
        request_power_action(POWER_RESTART);
    } else if (!strcmp(c, "shutdown")) {
        request_power_action(POWER_SHUTDOWN);
    } else
        emit("Unknown command. Type help.");
}
static void open_file(int i) {
    MayVfsNode info;
    if (!may_vfs_info(i, &info)) return;
    if (info.directory) {
        folder = i;
        file_scroll = 0;
        selected = -1;
        return;
    }
    if (editor_changed || editor_ime.size) {
        notify("请先保存编辑器中的修改");
        return;
    }
    if (!info.read_only && info.size >= FILE_CAP) {
        notify("文件超出文本编辑器容量");
        return;
    }
    if (!info.read_only) {
        for (u32 at = 0; at < info.size; at++) {
            if (files[i].data[at] != 0) continue;
            notify("文件包含零字节，无法按文本编辑");
            return;
        }
    }
    if (!read_text_file(i, editor_buf)) return;
    editor_file = i;
    editor_reset_view();
    ime_clear(&editor_ime);
    editor_changed = 0;
    raise_window(3);
}
static void save_editor(void) {
    if (may_vfs_read_only(editor_file)) {
        notify("只读文件");
        return;
    }
    if (editor_file < 0 || editor_file >= FS_MAX || !files[editor_file].used) {
        notify("文件已不存在");
        return;
    }
    int changed = strcmp((char *)files[editor_file].data, editor_buf) != 0;
    copy((char *)files[editor_file].data, editor_buf, FILE_CAP);
    files[editor_file].size = strlen(editor_buf);
    int ok = changed ? sync_changes() : disk_ready && sync_workspace();
    editor_changed = 0;
#ifdef MAYOS_LINUX
    notify(ok ? "已写入 Linux 持久化快照"
              : disk_ready ? "持久化写入失败：修改仍留在 RAM"
                           : "已保存到当前 RAM 会话；重启后丢失");
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
    ime_clear(&modal_ime);
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
        if (!fs_rename(modal_target, modal_text)) {
            notify("名称不可用或已存在");
            return;
        }
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
    const u32 dark_primary[] = {MAY_DARK_ACTION, 0x80d5ce, 0xafc6ff, 0xffb4a6, 0xf5ca6e, 0xcba9f8};
    const u32 light_primary[] = {0x252529, 0x006a64, 0x415f91, 0x904b40, 0x805700, 0x72518c};
    bg = dark ? MAY_DARK_BACKGROUND : 0xf5f5f6;
    panel = dark ? MAY_DARK_PANEL : 0xffffff;
    surface_high = dark ? MAY_DARK_SURFACE : 0xf0f0f2;
    ink = dark ? MAY_DARK_INK : 0x19191c;
    muted = dark ? MAY_DARK_MUTED : 0x626268;
    line = dark ? MAY_DARK_LINE : 0xd9d9dd;
    accent = dark ? dark_primary[accent_choice] : light_primary[accent_choice];
    on_accent = dark ? MAY_DARK_ON_ACTION : 0xffffff;
    selected_fill = dark ? MAY_DARK_SELECTION : 0xe4e4e8;
    if (high_contrast) {
        ink = dark ? 0xffffff : 0x08080a;
        muted = dark ? 0xf1f1f3 : 0x222225;
        line = dark ? 0x88888c : 0x66666a;
        selected_fill = dark ? 0x404045 : 0xc7c7ca;
    }
}
/* Reference desktop: macOS-like top menu and dock, with existing MD3 windows.
 * No misleading system APIs are exposed: status icons are informative.
 * Wallpaper lines and Notes.txt remain part of the default desktop. */
static void document_symbol(int x, int y, int size, u32 color) {
    int x0 = x + size / 4, y0 = y + size / 8;
    int x1 = x + size * 3 / 4, y1 = y + size * 7 / 8;
    stroke(x0, y0, x1 - size / 5, y0, 2, color);
    stroke(x1 - size / 5, y0, x1, y0 + size / 5, 2, color);
    stroke(x1, y0 + size / 5, x1, y1, 2, color);
    stroke(x1, y1, x0, y1, 2, color);
    stroke(x0, y1, x0, y0, 2, color);
    stroke(x1 - size / 5, y0, x1 - size / 5, y0 + size / 5, 2, color);
    stroke(x1 - size / 5, y0 + size / 5, x1, y0 + size / 5, 2, color);
    stroke(x0 + 6, y0 + size / 2, x1 - 5, y0 + size / 2, 2, color);
    stroke(x0 + 6, y0 + size / 2 + 7, x1 - 5, y0 + size / 2 + 7, 2, color);
}
static void desktop_wallpaper(void) {
    /* Pure-black monochrome with two thin silver arcs; native framebuffer only. */
    if (!high_contrast && desktop_style == DESKTOP_BING && wallpaper_date[0]) {
        memcpy(pixels, daily_wallpaper, sizeof(daily_wallpaper));
    } else if (desktop_style == DESKTOP_GRADIENT || desktop_style == DESKTOP_BING) {
        for (int y = 0; y < H; y++)
            rect(0, y, W, 1, blend(dark ? 0x030304 : 0xf9f9fb,
                                   dark ? 0x151518 : 0xe7e7eb, (u32)y * 120 / H));
        for (int x = 0; x < W; x++) {
            /* Two broad, softly shaded waves matching the reference wallpaper. */
            int upper = 128 - x / 3 + (x * x) / 4200;
            int lower = 692 - x / 4 - ((W - x) * (W - x)) / 5000;
            for (int d = -8; d <= 8; d++) {
                u32 a = (u32)(8 - (d < 0 ? -d : d)) * 17;
                if (upper + d > 31 && upper + d < H)
                    rect(x, upper + d, 1, 1, blend(0x050506, 0x56565a, a));
                if (lower + d > 31 && lower + d < H)
                    rect(x, lower + d, 1, 1, blend(0x050506, 0x414145, a));
            }
        }
    } else if (desktop_style == DESKTOP_SOLID) {
        rect(0, 0, W, H, bg);
    } else {
        rect(0, 0, W, H, bg);
        for (int x = 0; x < W; x += 64) rect(x, 42, 1, H - 42, line);
        for (int y = 42; y < H; y += 64) rect(0, y, W, 1, line);
    }
}
static const char *menu_label(enum DesktopMenu menu) {
    switch (menu) {
    case MENU_MAYOS: return "MayOS";
    case MENU_APP: return window_menu_name(focus);
    case MENU_FILE: return focus == 0 || focus == 3 ? L(FILE) : 0;
    case MENU_EDIT: return focus == 0 || focus == 3 ? L(EDIT) : 0;
    case MENU_GO: return focus == 0 ? pointer_label("前往", "Go") : 0;
    case MENU_SECTIONS: return focus == 1 ? pointer_label("分类", "Sections") : 0;
    case MENU_INPUT: return focus == 3 ? pointer_label("输入", "Input") : 0;
    case MENU_SHELL: return focus == 2 ? "Shell" : 0;
    case MENU_NAVIGATE: return focus == 4 ? pointer_label("导航", "Navigate") : 0;
    case MENU_VIEW: return focus < 0 ? L(VIEW) : 0;
    case MENU_WINDOW:
        for (int i = 0; i < WINDOW_COUNT; i++)
            if (windows[i].open) return L(WINDOW);
        return 0;
    case MENU_HELP: return L(HELP);
    default: return 0;
    }
}

static void desktop(void) {
    desktop_wallpaper();
    rect(0, 0, W, 32, dark ? 0x050506 : 0xf9f9fb);
    rect(0, 31, W, 1, dark ? 0x171719 : line);
    /* Compact monitor mark and dynamically spaced menus.  Never place a
       label at a guessed x coordinate: font metrics decide the next origin. */
    int menu_x = 6;
    for (int i = 0; i < MENU_COUNT; i++) {
        const char *label = menu_label(i);
        if (!label) {
            menu_left[i] = menu_right[i] = 0;
            continue;
        }
        int padding = i == MENU_MAYOS ? 40 : 11;
        int width = text_width(label) + padding + 11;
        menu_left[i] = menu_x;
        menu_right[i] = menu_x + width;
        if (active_menu == i || (active_menu < 0 && inside(mx, my, menu_x, 1, width, 30)))
            soft_roundrect(menu_x, 2, width, 28, 8, selected_fill);
        text(menu_x + padding, 6, label, active_menu == i || i == MENU_APP ? ink : muted);
        menu_x += width + 2;
    }
    soft_roundrect(18, 8, 18, 14, 3, ink);
    soft_roundrect(20, 10, 14, 10, 2,
                   active_menu == MENU_MAYOS || (active_menu < 0 &&
                   inside(mx, my, menu_left[0], 1, menu_right[0] - menu_left[0], 30))
                   ? selected_fill : dark ? 0x050506 : 0xf9f9fb);
    rect(25, 22, 4, 2, ink);
    rect(22, 24, 10, 1, ink);
    int count = 0;
    for (int i = 4; i < FS_MAX && count < 15; i++) {
        if (!files[i].used || files[i].parent != 2) continue;
        int x = 125 + (count % 5) * 124, y = 96 + (count / 5) * 100;
        int hover = inside(mx, my, x, y, 104, 90);
        if ((desktop_style == DESKTOP_BING && wallpaper_date[0]) || selected == i || hover ||
            (desktop_note_selected && !strcmp(files[i].name, "Notes.txt")))
            ui_card(x + 12, y - 3, 68, 79, 17, selected_fill);
        icon(x + 21, y, files[i].dir ? 0 : 5, 50);
        text(x, y + 58, files[i].name, ink);
        count++;
    }
    const int status_icons[] = {4, 8, 7, 12};
    for (int i = 0; i < 4; i++)
        symbol(TOP_STATUS_LEFT + i * TOP_STATUS_STEP, 7, status_icons[i], 18, muted);
    const char *clock_label = "Tue Jan 28  10:24";
    text(W - 18 - (int)strlen(clock_label) * 8, 6, clock_label, muted);
}
static int window_control_x(const Window *w, int index) {
    return w->w - 14 - WINDOW_CONTROL_SIZE -
           (2 - index) * (WINDOW_CONTROL_SIZE + WINDOW_CONTROL_GAP);
}
#ifdef MAYOS_LINUX
enum { WINDOW_RADIUS = 12, WINDOW_INNER_RADIUS = WINDOW_RADIUS - 1 };

/* Keep background and antialiased borders where the rectangular client
 * surface meets either bottom corner. The title bar reserves the top corners. */
static void window_bottom_corners(const Window *w,
    u32 saved[2][WINDOW_INNER_RADIUS][WINDOW_INNER_RADIUS], int restore) {
    const int r = WINDOW_INNER_RADIUS;
    for (int row = 0; row < r; row++) {
        int y = w->y + w->h - 2 - row;
        if (y < 0 || y >= H) continue;
        int dy = (r - row - 1) * 2 + 1;
        for (int col = 0; col < r; col++) {
            int dx = (r - col - 1) * 2 + 1;
            if (dx * dx + dy * dy <= 4 * r * r) break;
            int columns[] = {w->x + 1 + col, w->x + w->w - 2 - col};
            for (int side = 0; side < 2; side++) {
                int x = columns[side];
                if (x < 0 || x >= W) continue;
                if (restore) pixels[y * W + x] = saved[side][row][col];
                else saved[side][row][col] = pixels[y * W + x];
            }
        }
    }
}
#endif

static void frame(int id) {
    Window *w = &windows[id];
#ifdef MAYOS_LINUX
    u32 edge = focus == id && !high_contrast
        ? (dark ? 0x5b5c64 : 0xa6a8b0) : line;
    soft_roundrect(w->x, w->y, w->w, w->h, WINDOW_RADIUS, edge);
    soft_roundrect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, WINDOW_RADIUS - 1, panel);
#else
    soft_roundrect(w->x + 8, w->y + 12, w->w, w->h, 25,
                   dark ? 0x010102 : 0xcccdd1);
    ui_card(w->x, w->y, w->w, w->h, 25, panel);
#endif
    if (id != 1) {
        rect(w->x + 24, w->y + 43, w->w - 48, 1, line);
        symbol(w->x + 18, w->y + 11, id == 0 ? 0 : id == 4 ? 4 : id == 3 ? 5 : 2, 22, muted);
        const char *titles[] = {L(FILES), L(SETTINGS), L(TERMINAL), L(EDITOR)};
        text(w->x + 48, w->y + 11, id < 4 ? titles[id] : w->title, focus == id ? ink : muted);
#ifndef MAYOS_LINUX
        for (int i = 0; i < 3; i++)
            stroke(w->x + w->w - 8 - i * 5, w->y + w->h - 4,
                   w->x + w->w - 4, w->y + w->h - 8 - i * 5, 1, muted);
#endif
    }
    const int kinds[] = {15, w->max ? 29 : 16, 14};
    for (int i = 0; i < 3; i++) {
        int x = w->x + window_control_x(w, i), y = w->y + WINDOW_CONTROL_TOP;
        int hover = focus == id && inside(mx, my, x, y, WINDOW_CONTROL_SIZE, WINDOW_CONTROL_SIZE);
        if (hover) soft_roundrect(x, y, WINDOW_CONTROL_SIZE, WINDOW_CONTROL_SIZE, 9, selected_fill);
        symbol(x + 6, y + 6, kinds[i], 20, hover ? ink : muted);
    }
#ifndef MAYOS_LINUX
    if (high_contrast)
        border(w->x + 2, w->y + 2, w->w - 4, w->h - 4, line);
#endif
}
static const char *pointer_label(const char *zh, const char *en) {
    return display_language == MAY_ZH_HANS || display_language == MAY_ZH_HANT ? zh : en;
}

/* Keep the legacy desktop controls and Windows pointer choices in one Settings tab. */
static void personalization(int x, int y, int width) {
    heading_text(x, y - 8, pointer_label("个性化", "Personalization"), ink);
    button(x, y + 33, 142, pointer_label("鼠标指针", "Mouse pointer"), !desktop_options);
    button(x + 154, y + 33, 142, pointer_label("桌面", "Desktop"), desktop_options);
    if (desktop_options) {
        text(x, y + 81, pointer_label("桌面背景", "Background"), ink);
        const char *styles[] = {pointer_label("柔和渐变", "Gradient"), pointer_label("纯色背景", "Solid"),
                                pointer_label("网格背景", "Grid"), pointer_label("Bing 每日", "Bing daily")};
        for (int i = 0; i < DESKTOP_STYLE_COUNT; i++)
            button(x + i * 132, y + 108, 122, styles[i], desktop_style == i);
        if (desktop_style == DESKTOP_BING) {
            if (wallpaper_date[0]) {
                int end = text(x, y + 150, pointer_label("Bing 壁纸 · ", "Bing wallpaper · "), muted);
                text(end, y + 150, wallpaper_date, muted);
            } else {
#ifdef MAYOS_LINUX
                text(x, y + 150, pointer_label("等待 Bing，暂用自带背景。", "Waiting for Bing. Using the built-in background."), muted);
#elif defined(MAYOS_USE_BEARSSL)
                unsigned days, seconds;
                const char *status = may_time_read(&days, &seconds)
                    ? pointer_label("等待 Bing，暂用自带背景。", "Waiting for Bing. Using the built-in background.")
#ifdef MAYOS_ROUGHTIME
                    : pointer_label("正在等待已签名的网络时间。", "Waiting for signed network time.");
#else
                    : pointer_label("验签 UTC 后自动下载。", "Verify signed UTC to start automatic downloads.");
#endif
                text(x, y + 150, status, muted);
#else
                text(x, y + 150, pointer_label("当前构建使用离线壁纸。", "This build uses offline wallpapers."), muted);
#endif
            }
        }
        text(x, y + 188, pointer_label("桌面版本标识", "Desktop version label"), ink);
        button(x, y + 216, 142, pointer_label("显示", "Show"), show_brand);
        button(x + 154, y + 216, 142, pointer_label("隐藏", "Hide"), !show_brand);
        button(x, y + 290, 174, pointer_label("恢复默认设置", "Restore defaults"), 0);
        return;
    }
    text(x, y + 80, pointer_label("样式", "Style"), muted);
    button(x + 96, y + 75, 128, pointer_label("黑色指针", "Dark pointer"), cursor_style == POINTER_DARK);
    button(x + 230, y + 75, 128, pointer_label("白色默认", "White default"), cursor_style == POINTER_LIGHT);
    button(x + 364, y + 75, 148, pointer_label("恢复默认", "Restore default"), 0);
    text(x, y + 118, pointer_label("大小", "Size"), muted);
    const char *sizes[] = {pointer_label("小", "Small"), pointer_label("标准", "Standard"),
                           pointer_label("大", "Large")};
    for (int i = 0; i < 3; i++)
        button(x + 96 + i * 101, y + 113, 94, sizes[i], pointer_size == i);

    int card_y = y + 158;
    ui_card(x, card_y, width, 155, 16, bg);
    text(x + 14, card_y + 9, pointer_label("自定义指针", "Custom pointer"), ink);
    const char *sources[] = {"Cursor.ppm", "Cursor.cur", "Cursor.ani",
                             pointer_label("镜像导入", "ISO import")};
    for (int i = 0; i < POINTER_SOURCES; i++)
        button(x + 14 + i % 2 * 142, card_y + 38 + i / 2 * 36, 133,
               sources[i], cursor_choice == i);

    int preview_x = x + width - 170, preview_y = card_y + 18;
    ui_card(preview_x, preview_y, 150, 115, 10, panel);
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 8; col++)
            rect(preview_x + 10 + col * 16, preview_y + 10 + row * 16, 16, 16,
                 (row + col) & 1 ? surface_high : selected_fill);
    if (preview_cursor.count) {
        u32 index = may_cursor_frame(&preview_cursor, ticks - cursor_preview_start);
        const MayCursorFrame *frame = &preview_cursor.frames[index];
        draw_pointer_frame(frame, preview_x + 74, preview_y + 45, 36);
        text(preview_x + 10, preview_y + 93, preview_cursor.steps > 1 ?
            pointer_label("动画预览", "Animated preview") : pointer_label("静态预览", "Static preview"), muted);
    } else {
        text(preview_x + 10, preview_y + 93, pointer_label("没有预览", "No preview"), muted);
    }
    text(x + 14, card_y + 116, preview_cursor.count ?
         pointer_label("点击应用以更换指针", "Apply to change the pointer") :
         pointer_label("文件缺失或格式不支持", "Missing or unsupported file"), muted);
    button(x + width - 138, y + 326, 138, pointer_label("应用指针", "Apply pointer"),
           preview_cursor.count != 0);
    text(x, y + 333, cursor_style == POINTER_CUSTOM && cursor_active_source == cursor_choice ?
         pointer_label("当前使用此自定义指针", "Current custom pointer") :
         pointer_label("CUR、ANI 或旧版 PPM", "CUR, ANI or legacy PPM"), muted);
}

static const char *settings_label(int tab) {
    const char *labels[] = {L(GENERAL), L(DISPLAY), L(SOUND), L(NETWORK),
        pointer_label("个性化", "Personalization"), L(UPDATES), L(ABOUT),
        L(LANG_REGION), pointer_label("开源组件", "Open source")};
    return labels[tab];
}
static int settings_match(int tab) {
    const char *keywords[] = {"general device storage MayFS", "display resolution scale",
        "sound audio", "network ethernet ip", "personalization desktop pointer mouse",
        "updates update", "about version kernel", "language region locale", "open source licenses"};
    return contains(settings_label(tab), settings_search) || contains(keywords[tab], settings_search);
}
static void select_settings(int tab) {
    if (tab != settings_tab) {
        animate_window(1);
        language_choice = display_language;
        language_save_failed = 0;
        if (tab == SETTINGS_PERSONAL) {
            u32 size;
            int preset = cursor_style == POINTER_CUSTOM ? cursor_active_source :
                platform_pointer_asset(&size) ? POINTER_ISO : POINTER_CUR;
            cursor_select(preset, 0);
        }
    }
    settings_tab = tab;
    if (tab == SETTINGS_SOUND) sound_level = platform_audio_volume();
}
static void open_settings(int tab) {
    settings_search[0] = 0;
    settings_search_active = 0;
    raise_window(1);
    select_settings(tab);
}
#define SETTINGS_ROW_Y 128
#define SETTINGS_ROW_HEIGHT 32

static MayReleasePreview remote_release;
static int remote_loading, remote_result = -2;
#ifdef MAYOS_LINUX
static char update_target[24];
static int install_available;
#endif
/* The latest feed fits both transports and has an explicit end marker. */
static char remote_response[2048];
static int explorer_loading;

static void remote_update_start(void) {
    static const char url[] =
        "https://raw.githubusercontent.com/ZhongYeMay/MayOS/main/website/update-latest-beta-v1.txt";
    if (explorer_loading) {
        copy(remote_response, "Explorer is loading a page. Try again shortly.", sizeof(remote_response));
        remote_result = -1;
        return;
    }
    remote_loading = platform_web_start(url);
    if (!remote_loading) {
        platform_web_poll(remote_response, sizeof(remote_response));
        remote_result = -1;
    }
}

static void remote_update_poll(void) {
    if (!remote_loading) return;
    remote_loading = platform_web_poll(remote_response, sizeof(remote_response));
    if (!remote_loading) {
#ifdef MAYOS_LINUX
        int linux_desktop = 1;
#else
        int linux_desktop = 0;
#endif
        remote_result = feed_parse(remote_response, MAYOS_VERSION, linux_desktop, &remote_release);
#ifdef MAYOS_LINUX
        install_available = platform_install_target(update_target, sizeof(update_target)) &&
            feed_parse(remote_response, "0.0.0", 2, &remote_release) == 1 && remote_release.url[0];
        if (install_available) remote_result = 1;
#endif
        if (remote_result >= 0) serial("MayOS: GitHub release feed parsed\n");
        dirty = 1;
    }
}

static void update_notes(int x, int y, int width, const char *value) {
    for (int row = 0; row < 2 && *value; row++) {
        char line_text[110];
        int length = 0, last_space = 0;
        while (value[length] && length < (int)sizeof(line_text) - 1) {
            int bytes = 1;
            while (bytes < 4 && (value[length + bytes] & 0xc0) == 0x80) bytes++;
            if (length + bytes >= (int)sizeof(line_text)) break;
            memcpy(line_text + length, value + length, (u32)bytes);
            line_text[length + bytes] = 0;
            if (text_width(line_text) > width) break;
            if (value[length] == ' ') last_space = length;
            length += bytes;
        }
        if (!length) return;
        if (value[length] && last_space) length = last_space;
        line_text[length] = 0;
        text(x, y + 18 * row, line_text, muted);
        value += length;
        while (*value == ' ') value++;
    }
}

static const char *browser_button(void) {
#ifdef MAYOS_LINUX
    if (platform_browser_ready()) return pointer_label("打开 Firefox", "Open Firefox");
#endif
    return pointer_label("打开 Explorer", "Open Explorer");
}

static void settings(Window *w) {
    int x = w->x, y = w->y, cx = x + 264, cy = y + 53, cw = w->w - 286;
    rect(x + 1, y + 36, 240, w->h - 38, dark ? 0x080809 : 0xf7f7f9);
    rect(x + 240, y + 36, 1, w->h - 38, line);
    heading_text(x + 20, y + 49, L(SETTINGS), ink);
    ui_card(x + 18, y + 82, 207, 34, 17, settings_search_active ? selected_fill :
            dark ? 0x111113 : surface_high);
    symbol(x + 28, y + 90, 19, 15, muted);
    text(x + 51, y + 91, settings_search[0] ? settings_search : L(SEARCH_SETTINGS),
         settings_search[0] ? ink : muted);
    if (settings_search[0]) symbol(x + 204, y + 91, 14, 14, muted);
    const int tab_icons[] = {1, 23, 24, 8, 25, 26, 27, 28, 5};
    int row = 0;
    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        if (!settings_match(i)) continue;
        int yy = y + SETTINGS_ROW_Y + row++ * SETTINGS_ROW_HEIGHT;
        if (settings_tab == i)
            soft_roundrect(x + 18, yy, 207, 29, 9, selected_fill);
        symbol(x + 29, yy + 5, tab_icons[i], 18, settings_tab == i ? ink : muted);
        text(x + 61, yy + 4, settings_label(i), settings_tab == i ? ink : muted);
    }
    if (!row) text(x + 18, y + SETTINGS_ROW_Y + 7, pointer_label("没有匹配项目", "No matching settings"), muted);

    if (settings_tab == SETTINGS_LANGUAGE) {
        heading_text(cx, cy - 8, L(LANG_REGION), ink);
        text(cx, cy + 28, L(DISPLAY_LANGUAGE), muted);
        text_face(cx + cw - text_width(languages[display_language].name), cy + 28,
                  languages[display_language].name, ink, display_language);
        language_grid(cx, cy + 57, cw, language_choice, ink, muted);
        int changed = language_choice != display_language;
        if (changed) {
            button(cx, cy + 273, 118, L(CANCEL), 0);
            button(cx + cw - 136, cy + 273, 136, L(APPLY), 1);
        }
        if (language_save_failed) text(cx, cy + 316, L(SAVE_ERROR), ink);
        else if (!disk_ready) text(cx, cy + 316, L(SESSION_ONLY), muted);
    } else if (settings_tab == SETTINGS_ABOUT) {
        heading_text(cx, cy - 8, L(ABOUT), ink);
        /*
         * Supplied reference: 1320x580 card with black #000 interior,
         * #292929 rounded outline, white monitor+name and muted gray version.
         * Native 1280x720 layout follows the approved 16:9 reference.
         */
        const int card_top = cy + 48, card_height = 230;
        u32 card_bg = dark ? 0x000000 : 0xffffff;
        ui_card(cx, card_top, cw, card_height, 24, card_bg);
        /* Draw the monitor directly (no generic gear tile).
         * Original Easter-egg click region at (220,190) is unchanged. */
        reference_monitor_icon(cx + 18, card_top + 31, ink, card_bg);
        reference_text(cx + 57, card_top + 18, "MayOS", ink, 1);
        reference_text(cx + 57, card_top + 48, "Version " MAYOS_RELEASE, muted, 0);
        rect(cx + 18, card_top + 96, cw - 36, 1, line);
        /* The three rows intentionally have no icons, chips or secondary
           descriptions: exact hierarchy of the approved reference. */
        reference_text(cx + 18, card_top + 126, "Kernel", muted, 0);
#ifdef MAYOS_LINUX
        reference_right_label(cx + cw - 18, card_top + 126, "Linux x86_64", ink);
#else
        reference_right_label(cx + cw - 18, card_top + 126, "MayKernel x86_64", ink);
#endif
        reference_text(cx + 18, card_top + 158, "Build", muted, 0);
        reference_right_label(cx + cw - 18, card_top + 158, "Development Preview", ink);
        reference_text(cx + 18, card_top + 190, "Architecture", muted, 0);
        reference_right_label(cx + cw - 18, card_top + 190, "64-bit", ink);
        /* Device rename/Credits remain available beneath the card,
           outside the screenshot-specific component. */
    } else if (settings_tab == SETTINGS_UPDATES) {
        heading_text(cx, cy - 8, L(UPDATES), ink);
        ui_card(cx, cy + 29, cw, 103, 18, bg);
        reference_text(cx + 18, cy + 42, "MayOS " MAYOS_VERSION, ink, 1);
        text(cx + 18, cy + 88, platform_kernel(), muted);
#ifdef MAYOS_LINUX
        cy -= 12;
#endif
        button(cx, cy + 149, 200,
               remote_loading ? pointer_label("检查中…", "Checking…") :
               pointer_label("从 GitHub 检查", "Check GitHub"), !remote_loading);
        if (!remote_loading) {
            const char *state = remote_result == -2 ? pointer_label("尚未检查", "Not checked") :
                remote_result < 0 ? pointer_label("无法验证 GitHub 更新列表", "Could not read GitHub release feed") :
                remote_result == 0 ? pointer_label("没有更新版本", "No newer version") :
                #ifdef MAYOS_LINUX
                install_available ? pointer_label("可安装的 MayKernel 版本", "MayKernel release for selected disk") :
#endif
                pointer_label("发现新版本", "New version available");
            text(cx, cy + 197, state, ink);
            if (remote_result == 1) {
                text(cx, cy + 225, remote_release.name, ink);
                text(cx + 256, cy + 225, remote_release.tag, muted);
                text(cx, cy + 250, remote_release.date, muted);
                text(cx + 132, cy + 250, "x86_64", muted);
                text(cx + 230, cy + 250, remote_release.kernel, muted);
                text(cx, cy + 275, pointer_label("镜像大小", "Image size"), muted);
                u32 size_tenths = (u32)(((u64)remote_release.bytes * 10u + 524288u) / 1048576u);
                char size[28], fraction[12];
                number(size, size_tenths / 10u);
                copy(size + strlen(size), ".", sizeof(size) - strlen(size));
                number(fraction, size_tenths % 10u);
                copy(size + strlen(size), fraction, sizeof(size) - strlen(size));
                copy(size + strlen(size), " MiB", sizeof(size) - strlen(size));
                text(cx + 138, cy + 275, size, ink);
                update_notes(cx, cy + 296, cw, remote_release.summary);
                text(cx, cy + 334, pointer_label("安装预计数分钟，另计重启时间",
                                                  "Installation estimate: a few minutes, plus restarts"), muted);
#ifdef MAYOS_LINUX
                if (media_state == 1 || media_state == 3) {
                    text(cx, cy + 348, media_state == 3 ? pointer_label("正在写入备用槽", "Installing to spare slot") :
                                                         pointer_label("正在下载并校验", "Downloading and verifying"), muted);
                    int span = cw - 68;
                    soft_roundrect(cx, cy + 376, span, 8, 4, line);
                    int fill = span * media_percent / 100;
                    if (fill) soft_roundrect(cx, cy + 376, fill, 8, 4, accent);
                    numtext(cx + span + 10, cy + 368, (u32)media_percent, ink);
                    text(cx + span + 36, cy + 368, "%", ink);
                } else if (media_state == 4) {
                    text(cx, cy + 355, pointer_label("安装完成，重启后试用新版", "Installed. Restart to try the new version."), ink);
                } else if (media_state == 2) {
                    text(cx, cy + 358, pointer_label("已校验并保存 /tmp/MayOS-update.iso",
                                                     "Verified and saved /tmp/MayOS-update.iso"), muted);
                } else if (remote_release.url[0]) {
                    button(cx, cy + 355, 230, install_available ?
                           pointer_label("下载并安装 MayKernel", "Download and install MayKernel") :
                           pointer_label("下载 ISO", "Download ISO"), media_state >= 0);
                    if (install_available) text(cx + 242, cy + 362, update_target, muted);
                    if (media_state < 0) text(cx + 332, cy + 362, pointer_label("更新失败", "Update failed"), muted);
                }
#else
                text(cx, cy + 358, pointer_label("请从发布页获取并启动安装 ISO",
                                                  "Get the installer ISO from GitHub Releases"), muted);
#endif
            } else if (remote_result < 0 && remote_result != -2 && remote_response[0] &&
                       text_width(remote_response) <= cw) {
                text(cx, cy + 227, remote_response, muted);
            }
        }
    } else if (settings_tab == SETTINGS_DISPLAY) {
        MayDisplayInfo info;
        display_get_info(&info);
        heading_text(cx, cy - 8, L(DISPLAY), ink);
        ui_card(cx, cy + 40, cw, 116, 18, bg);
        reference_monitor_icon(cx + 18, cy + 62, ink, bg);
        text(cx + 62, cy + 57, pointer_label("当前显示器", "Current display"), ink);
        char value[48];
        number(value, info.width);
        int length = strlen(value);
        copy(value + length, " × ", sizeof(value) - length);
        number(value + strlen(value), info.height);
        text(cx + 62, cy + 84, value, muted);
        number(value, info.bpp);
        copy(value + strlen(value), "-bit", sizeof(value) - strlen(value));
        text(cx + cw - 18 - text_width(value), cy + 84, value, muted);
        text(cx + 18, cy + 123, pointer_label("刷新率", "Refresh rate"), muted);
        const char *refresh = pointer_label("由固件管理", "Firmware managed");
        if (info.refresh_known) {
            number(value, info.refresh_hz);
            copy(value + strlen(value), " Hz", sizeof(value) - strlen(value));
            refresh = value;
        }
        text(cx + cw - 18 - text_width(refresh), cy + 123, refresh, ink);
        text(cx, cy + 178, pointer_label("显示缩放", "Display scaling"), ink);
        const char *modes[] = {pointer_label("自动", "Auto"), "100%",
                              pointer_label("适合", "Fit"), pointer_label("填满", "Fill")};
        for (int i = 0; i < 4; i++)
            button(cx + i * 132, cy + 205, 122, modes[i], info.scale_mode == i);
        text(cx, cy + 257, pointer_label("实际缩放", "Effective scale"), muted);
        number(value, info.scale_percent);
        copy(value + strlen(value), "%", sizeof(value) - strlen(value));
        text(cx + cw - text_width(value), cy + 257, value, ink);
        if (!display_trial_deadline)
            text(cx, cy + 294, pointer_label("自动模式等比缩放桌面。", "Auto scales the desktop proportionally."), muted);
        if (display_trial_deadline) {
            text(cx, cy + 294, pointer_label("恢复倒计时", "Reverting in"), muted);
            number(value, display_trial_seconds);
            copy(value + strlen(value), " s", sizeof(value) - strlen(value));
            text(cx + 174, cy + 294, value, ink);
            button(cx, cy + 320, 150, pointer_label("恢复上一模式", "Revert"), 0);
            button(cx + 163, cy + 320, 150, pointer_label("保留此模式", "Keep mode"), 1);
        } else if (platform_display_has_modes()) {
            button(cx, cy + 320, 150, pointer_label("上一显示模式", "Previous mode"), 0);
            button(cx + 163, cy + 320, 150, pointer_label("下一显示模式", "Next mode"), 0);
        } else {
            text(cx, cy + 322, pointer_label("输出分辨率由启动显示模式决定。", "Output resolution follows the boot display mode."), muted);
        }
    } else if (settings_tab == SETTINGS_GENERAL) {
        heading_text(cx, cy - 8, L(GENERAL), ink);
        ui_card(cx, cy + 39, cw, 109, 16, bg);
        text(cx + 17, cy + 51, pointer_label("设备与账户", "Device and account"), muted);
        text(cx + 17, cy + 78, device, ink);
        text(cx + 17, cy + 113, profile_name, muted);
        button(cx + cw - 163, cy + 105, 146, pointer_label("重命名会话设备", "Rename for session"), 0);
        ui_card(cx, cy + 160, cw, 103, 16, bg);
        text(cx + 17, cy + 172, pointer_label("存储与会话", "Storage and session"), muted);
#ifdef MAYOS_LINUX
        const char *storage = pointer_label("已启用 ext4 持久化", "Marked ext4 profile enabled");
#else
        const char *storage = pointer_label("已连接 MayFS 数据盘", "MayFS data disk connected");
#endif
        text(cx + 17, cy + 199, disk_ready ? storage :
             pointer_label("当前文件保存在会话内存中", "Files are stored in session RAM"), ink);
        text(cx + 17, cy + 230, PLATFORM_SHORT, muted);
        if (disk_ready) button(cx + cw - 163, cy + 194, 146, pointer_label("同步磁盘", "Sync disk"), 0);
        button(cx, cy + 284, 174, L(LANG_REGION), 0);
        button(cx + 186, cy + 284, 186, L(ABOUT), 0);
    } else if (settings_tab == SETTINGS_SOUND) {
        heading_text(cx, cy - 8, L(SOUND), ink);
        ui_card(cx, cy + 39, cw, 150, 16, bg);
        text(cx + 17, cy + 56, pointer_label("输出音量", "Output volume"), ink);
        if (sound_level >= 0) {
            char label[20];
            number(label, (u32)sound_level);
            copy(label + strlen(label), "%", sizeof(label) - strlen(label));
            reference_right_label(cx + cw - 18, cy + 57, label, muted);
            int span = cw - 112, amount = span * sound_level / 100;
            soft_roundrect(cx + 56, cy + 119, span, 8, 4, line);
            if (amount) soft_roundrect(cx + 56, cy + 119, amount, 8, 4, accent);
            circle(cx + 56 + amount, cy + 123, 8, ink);
            button(cx + 15, cy + 107, 32, "-", 0);
            button(cx + cw - 47, cy + 107, 32, "+", 0);
        } else {
            text(cx + 17, cy + 104, pointer_label("没有可调节的输出设备", "No adjustable output available"), muted);
        }
    } else if (settings_tab == SETTINGS_NETWORK) {
        heading_text(cx, cy - 8, L(NETWORK), ink);
        ui_card(cx, cy + 39, cw, 145, 16, bg);
#ifdef MAYOS_LINUX
        char report[384];
        int online = platform_network_status(report, sizeof(report));
        text(cx + 17, cy + 59, online > 0 ? pointer_label("网络接口已有 IP 地址", "An interface has an IP address") :
             pointer_label("网络未就绪", "Network is not ready"), ink);
#else
        const MayNetInfo *nic = may_net_info();
        text(cx + 17, cy + 59, nic->present ? pointer_label("检测到以太网适配器", "Ethernet adapter detected") :
             pointer_label("未检测到受支持的以太网适配器", "No supported Ethernet adapter detected"), ink);
        const MayDhcpClient *lease = may_net_dhcp_info();
        enum MayNetState state = may_net_state();
        if (state != MAY_NET_ABSENT) {
            const char *connection = state == MAY_NET_IPV4_ASSIGNED ? may_net_format_ipv4(lease->address) :
                state == MAY_NET_LINK_DOWN ? pointer_label("网线未连接", "Ethernet link down") :
                pointer_label("等待自动获取 IPv4 地址", "Waiting for an IPv4 address");
            text(cx + 17, cy + 94, connection, muted);
        }
        text(cx + 17, cy + 132, "RX", muted);
        numtext(cx + 54, cy + 132, nic->packets_rx, ink);
        text(cx + 214, cy + 132, "TX", muted);
        numtext(cx + 251, cy + 132, nic->packets_tx, ink);
#endif
        button(cx + 17, cy + 231, 192, browser_button(), 0);
    } else if (settings_tab == SETTINGS_CREDITS) {
        heading_text(cx, cy - 8, settings_label(SETTINGS_CREDITS), ink);
        ui_card(cx, cy + 39, cw, 304, 16, bg);
        const char *const names[] = {"GNU GRUB", "Linux", "BusyBox", "musl", "Fastfetch", "QEMU / OVMF", "Noto CJK / DejaVu"};
        const char *const licenses[] = {"GPL-3.0-or-later", "GPL-2.0-only", "GPL-2.0-only", "MIT", "MIT", "GPL-2.0 / BSD-2-Clause-Patent", "OFL / Bitstream Vera"};
        for (int i = 0; i < 7; i++) {
            int yy = cy + 51 + i * 40;
            text(cx + 17, yy, names[i], ink);
            text(cx + cw - 17 - text_width(licenses[i]), yy, licenses[i], muted);
            if (i < 6) rect(cx + 17, yy + 30, cw - 34, 1, line);
        }
    } else if (settings_tab == SETTINGS_PERSONAL) {
        personalization(cx, cy, cw);
    }
}
static int file_list[FS_MAX], file_count;
static void files_up(void) {
    MayVfsNode node;
    if (may_vfs_info(folder, &node)) folder = node.parent;
    file_scroll = 0;
    selected = -1;
}

static void build_file_list(void) {
    file_count = 0;
    for (int i = may_vfs_next(folder, -1); i >= 0 && file_count < FS_MAX; i = may_vfs_next(folder, i))
        file_list[file_count++] = i;
}
static void files_draw(Window *w) {
    int x = w->x, y = w->y;
    rect(x + 1, y + 43, 146, w->h - 44, bg);
    text(x + 20, y + 65, L(LOCATIONS), muted);
    const char *nav[] = {
#ifdef MAYOS_LINUX
        "RAM / 根目录",
#else
        "disk1 / 根目录",
#endif
        L(DOCUMENTS), L(DESKTOP), L(TRASH)};
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
    MayVfsNode node;
    if (folder && may_vfs_info(folder, &node))
        text(cx + 130, y + 59, node.name, ink);
    if (may_vfs_read_only(folder))
        text(cx + 10, y + 105, L(READ_ONLY), muted);
    else {
        button(cx, y + 99, 134, L(NEW_FILE), 0);
        button(cx + 144, y + 99, 148, L(NEW_FOLDER), 0);
    }
    button(x + w->w - 120, y + 99, 44, "‹", 0);
    button(x + w->w - 66, y + 99, 44, "›", 0);
    text(cx, y + 147, L(NAME), muted);
    text(x + w->w - 190, y + 147, L(TYPE), muted);
    text(x + w->w - 99, y + 147, L(SIZE), muted);
    rect(cx, y + 176, w->w - 185, 1, line);
    build_file_list();
    int visible = (w->h - 225) / 44;
    if (file_scroll >= file_count)
        file_scroll = 0;
    for (int k = 0; k + file_scroll < file_count && k < visible; k++) {
        int i = file_list[k + file_scroll], yy = y + 184 + k * 44;
        if (!may_vfs_info(i, &node)) continue;
        if (selected == i)
            roundrect(cx - 4, yy, w->w - 178, 40, selected_fill);
        icon(cx + 7, yy + 8, node.directory ? 0 : 2, 24);
        text(cx + 45, yy + 10, node.name, ink);
        text(x + w->w - 190, yy + 10, node.directory ? "文件夹" :
             node.read_only ? "系统文件" : "文件", muted);
        if (!node.directory && !node.read_only)
            numtext(x + w->w - 92, yy + 10, node.size, muted);
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
    rect(x, y, w->w - 2, w->h - 44, dark ? 0x09090a : 0xf6f6f8);
    int rows = (w->h - 108) / 21;
    int end = out_count - scroll;
    if (end < 0)
        end = 0;
    int start = end - rows;
    if (start < 0)
        start = 0;
    for (int i = start; i < end; i++)
        text(x + 18, y + 12 + (i - start) * 21, output[i], ink);
    int py = w->y + w->h - 48;
    int xx = text(x + 18, py, profile_name, accent);
    xx = text(xx, py, "@mayos", accent);
    xx = text(xx, py, ":~ $ ", muted);
    text(xx, py, command, ink);
    if (ticks % 100 < 55)
        rect(xx + cmdlen * 8, py + 3, 7, 16, accent);
    rect(x + 1, w->y + w->h - 29, w->w - 4, 1, line);
    text(x + 18, w->y + w->h - 24, "MayShell / " PLATFORM_SHORT, muted);
    text(x + w->w - 220, w->y + w->h - 24, "↑ 历史   ↓ 清空   F1 终端", muted);
}
enum { EDIT_IME_WIDTH = 120, EDIT_IME_HEIGHT = 95 };

static int editor_writable(void) {
    MayVfsNode node;
    return may_vfs_info(editor_file, &node) && !node.read_only;
}

static int ime_insert(char *buffer, u32 cap, const char *value) {
    u32 end = strlen(buffer), size = strlen(value);
    if (!size) return 1;
    if (buffer == editor_buf) return editor_insert(value, size);
    if (size >= cap - end) {
        notify(cap == FILE_CAP ? "UTF-8 · 4095 B" : "UTF-8 · 47 B");
        return 0;
    }
    memcpy(buffer + end, value, size + 1);
    return 1;
}

static int ime_commit_to(MayIme *ime, char *buffer, u32 cap, unsigned selected) {
    MayImeChoices choices;
    ime_convert(ime, display_language == MAY_ZH_HANT, &choices);
    char word[IME_TEXT];
    if (!ime_word(&choices, selected, word) || !ime_insert(buffer, cap, word)) return 0;
    ime_clear(ime);
    return 1;
}

static void ime_key(MayIme *ime, char *buffer, u32 cap, int key) {
    if (key == MAYOS_KEY_IME) {
        if (!ime->size || ime_commit_to(ime, buffer, cap, ime->selected))
            ime->mode = (ime->mode + 1) % IME_MODES;
        return;
    }
    if (key == 27) {
        ime_clear(ime);
        return;
    }
    if (key == '\b') {
        if (ime->size) {
            ime->raw[--ime->size] = 0;
            ime->selected = 0;
        } else if (buffer[0]) may_utf8_backspace(buffer);
        return;
    }
    if (ime->size) {
        MayImeChoices choices;
        ime_convert(ime, display_language == MAY_ZH_HANT, &choices);
        if (key == MAYOS_KEY_LEFT || key == 0x101 || key == MAYOS_KEY_RIGHT || key == 0x102) {
            if (choices.count) {
                unsigned delta = key == MAYOS_KEY_LEFT || key == 0x101 ? choices.count - 1 : 1;
                ime->selected = (ime->selected + delta) % choices.count;
            }
            return;
        }
        if (key == '\n' || key == ' ') {
            if (key == ' ' && ime->mode == IME_HIRAGANA && choices.count > 1)
                ime->selected = (ime->selected + 1) % choices.count;
            else if (ime_commit_to(ime, buffer, cap, ime->selected) && key == ' ' && ime->mode == IME_HANGUL)
                ime_insert(buffer, cap, " ");
            return;
        }
        if (ime->mode != IME_ZHUYIN && ime->mode != IME_UNICODE &&
            key >= '1' && key <= '0' + IME_PAGE && choices.count > 1) {
            ime_commit_to(ime, buffer, cap, ime->selected / IME_PAGE * IME_PAGE + key - '1');
            return;
        }
    }
    int lower = key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;
    int letter = lower >= 'a' && lower <= 'z';
    int compose = 0;
    switch (ime->mode) {
        case IME_PINYIN: compose = letter; key = lower; break;
        case IME_ZHUYIN: compose = key > 0 && key < 128 && ime_index(ime_zhuyin_keys, lower); key = lower; break;
        case IME_HIRAGANA:
        case IME_KATAKANA: compose = letter || key == '\'' || key == '-'; key = lower; break;
        case IME_HANGUL: compose = letter; break;
        case IME_UNICODE: compose = ime_hex_digit(key) < 16; break;
        default: break;
    }
    if (compose) {
        unsigned limit = ime->mode == IME_UNICODE ? 6 : IME_KEYS - 1;
        if (ime->size < limit) {
            ime->raw[ime->size++] = (char)key;
            ime->raw[ime->size] = 0;
            ime->selected = 0;
        }
        return;
    }
    if (key != '\n' && key != '\t' && !(key >= 32 && key < 127)) return;
    if (ime->size && !ime_commit_to(ime, buffer, cap, ime->selected)) return;
    char ascii[] = {(char)key, 0};
    ime_insert(buffer, cap, ascii);
}


static void editor_move(int key);
static void editor_key(int key) {
    if (key == 0x106) {
        if (editor_writable() && (!editor_ime.size ||
            ime_commit_to(&editor_ime, editor_buf, FILE_CAP, editor_ime.selected)))
            save_editor();
        return;
    }
    if (key == MAYOS_KEY_SELECT_ALL || key == MAYOS_KEY_COPY ||
        key == MAYOS_KEY_CUT || key == MAYOS_KEY_PASTE ||
        key == MAYOS_KEY_UNDO || key == MAYOS_KEY_REDO) {
        if (key == MAYOS_KEY_SELECT_ALL) {
            editor_anchor = 0;
            editor_caret = strlen(editor_buf);
            return;
        }
        if (editor_ime.size && key != MAYOS_KEY_COPY) return;
        if (key == MAYOS_KEY_COPY || key == MAYOS_KEY_CUT) {
            int begin = editor_caret < editor_anchor ? editor_caret : editor_anchor;
            int end = editor_caret > editor_anchor ? editor_caret : editor_anchor;
            if (begin == end) return;
            memcpy(editor_clipboard, editor_buf + begin, end - begin);
            editor_clipboard[end - begin] = 0;
            if (key == MAYOS_KEY_CUT && editor_writable()) editor_insert("", 0);
        } else if (editor_writable()) {
            if (key == MAYOS_KEY_PASTE && editor_clipboard[0])
                editor_insert(editor_clipboard, strlen(editor_clipboard));
            if (key == MAYOS_KEY_UNDO || key == MAYOS_KEY_REDO)
                editor_recover(key == MAYOS_KEY_REDO);
        }
        return;
    }
    int nav = key & ~MAYOS_KEY_EXTEND;
    if ((!editor_ime.size || (key & MAYOS_KEY_EXTEND)) &&
        (nav == MAYOS_KEY_LEFT || nav == MAYOS_KEY_RIGHT || nav == MAYOS_KEY_HOME ||
         nav == MAYOS_KEY_END || nav == 0x101 || nav == 0x102 ||
         nav == '\b' || nav == 0x103)) {
        if (editor_ime.size &&
            !ime_commit_to(&editor_ime, editor_buf, FILE_CAP, editor_ime.selected)) return;
        editor_move(key);
        return;
    }
    if (editor_writable()) ime_key(&editor_ime, editor_buf, FILE_CAP, nav);
}

static int editor_ime_left(const Window *w) { return w->x + w->w - 254; }
static int editor_ime_top(const Window *w) { return w->y + w->h - EDIT_IME_HEIGHT - 42; }
static int editor_ime_cell(const Window *w) { return (w->w - 68) / IME_PAGE; }

static void ime_draw(const MayIme *ime, int x, int y, int width) {
    ui_card(x, y, width, EDIT_IME_HEIGHT, 15, dark ? 0x111114 : 0xffffff);
    const char *raw = ime->raw;
    char phonetic[IME_TEXT];
    if (ime->mode == IME_ZHUYIN) {
        unsigned offset = 0;
        for (const char *p = ime->raw; *p; ++p) {
            unsigned index = ime_index(ime_zhuyin_keys, *p);
            if (index) offset = ime_write(phonetic, offset, ime_zhuyin_code[index - 1]);
        }
        phonetic[offset] = 0;
        raw = phonetic;
    }
    if (ime->mode == IME_UNICODE) text(x + 15, y + 12, "U+", muted);
    const char *visible = raw;
    while (*visible && text_width(visible) > width - 175) may_utf8_next(&visible);
    text(x + (ime->mode == IME_UNICODE ? 36 : 15), y + 12, visible, ink);
    text(x + width - 143, y + 12, "← →   Enter", muted);
    rect(x + 12, y + 40, width - 24, 1, line);
    MayImeChoices choices;
    ime_convert(ime, display_language == MAY_ZH_HANT, &choices);
    if (!choices.count) {
        text(x + 15, y + 56, ime->mode == IME_UNICODE ? "Invalid code point" : "No candidates", muted);
        return;
    }
    unsigned base = ime->selected / IME_PAGE * IME_PAGE;
    int cell = (width - 28) / IME_PAGE;
    for (unsigned i = 0; i < IME_PAGE && base + i < choices.count; ++i) {
        int left = x + 13 + cell * (int)i;
        int single = choices.count == 1;
        int span = single ? width - 26 : cell - 5;
        ui_card(left, y + 47, span, 35, 9, base + i == ime->selected ? selected_fill : surface_high);
        char label[] = {(char)('1' + i), 0};
        int numbered = !single && ime->mode != IME_ZHUYIN && ime->mode != IME_UNICODE;
        if (numbered) text(left + 8, y + 54, label, muted);
        char candidate[IME_TEXT];
        if (ime_word(&choices, base + i, candidate)) {
            char *end = candidate;
            const char *scan = candidate;
            int remaining = span - (numbered ? 31 : 15);
            while (*scan) {
                int used = glyph_width(may_utf8_next(&scan));
                if (remaining < used) break;
                remaining -= used;
                end = (char *)scan;
            }
            *end = 0;
            text(left + (numbered ? 23 : 8), y + 54, candidate, ink);
        }
    }
}

typedef struct { int byte, row, column; unsigned cp; } EditorCell;
static EditorCell editor_cells[FILE_CAP];
static int editor_cell_count;

static void editor_layout(const Window *w) {
    int row = 0, column = 0, columns = (w->w - 48) / 8;
    const char *p = editor_buf;
    editor_cell_count = 0;
    while (*p) {
        const char *start = p;
        unsigned cp = may_utf8_next(&p);
        if (cp == '\r' && *p == '\n') p++;
        int width = cp == '\t' ? 4 - column % 4 : glyph_width(cp) / 8;
        if (cp != '\r' && cp != '\n' && column + width > columns) {
            row++;
            column = 0;
            if (cp == '\t') width = 4;
        }
        editor_cells[editor_cell_count++] = (EditorCell){start - editor_buf, row, column, cp};
        if (cp == '\r' || cp == '\n') {
            row++;
            column = 0;
        } else if (cp != 0xfeff || start != editor_buf) column += width;
    }
    editor_cells[editor_cell_count] = (EditorCell){p - editor_buf, row, column, 0};
}

static int editor_cell_at(int byte) {
    int low = 0, high = editor_cell_count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (editor_cells[middle].byte < byte) low = middle + 1;
        else high = middle;
    }
    return low;
}

static int editor_nearest(int row, int column) {
    int best = 0, distance = 0x7fffffff;
    for (int i = 0; i <= editor_cell_count; i++) {
        const EditorCell *cell = &editor_cells[i];
        if (cell->row < row) { best = i; continue; }
        if (cell->row > row) break;
        int delta = column - cell->column;
        if (delta < 0) delta = -delta;
        if (delta <= distance) {
            best = i;
            distance = delta;
        }
    }
    return best;
}

static void editor_move(int key) {
    editor_layout(&windows[3]);
    int extend = key & MAYOS_KEY_EXTEND;
    key &= ~MAYOS_KEY_EXTEND;
    int current = editor_cell_at(editor_caret), next = current;
    EditorCell cell = editor_cells[current];
    switch (key) {
    case MAYOS_KEY_LEFT:
        if (!extend && editor_caret != editor_anchor)
            next = editor_cell_at(editor_caret < editor_anchor ? editor_caret : editor_anchor);
        else if (current > 0) next--;
        break;
    case MAYOS_KEY_RIGHT:
        if (!extend && editor_caret != editor_anchor)
            next = editor_cell_at(editor_caret > editor_anchor ? editor_caret : editor_anchor);
        else if (current < editor_cell_count) next++;
        break;
    case MAYOS_KEY_HOME: next = editor_nearest(cell.row, 0); break;
    case MAYOS_KEY_END: next = editor_nearest(cell.row, W); break;
    case 0x101: if (cell.row > 0) next = editor_nearest(cell.row - 1, cell.column); break;
    case 0x102:
        if (cell.row < editor_cells[editor_cell_count].row)
            next = editor_nearest(cell.row + 1, cell.column);
        break;
    case '\b': case 0x103:
        if (!editor_writable()) return;
        if (editor_caret != editor_anchor) editor_insert("", 0);
        else if (key == '\b' && current > 0)
            editor_replace(editor_cells[current - 1].byte, editor_caret, "", 0);
        else if (key == 0x103 && current < editor_cell_count)
            editor_replace(editor_caret, editor_cells[current + 1].byte, "", 0);
        return;
    }
    editor_caret = editor_cells[next].byte;
    if (!extend) editor_anchor = editor_caret;
}

static void editor_select_at(const Window *w, int x, int y) {
    editor_layout(w);
    int row = editor_scroll_row + (y > w->y + 116 ? (y - w->y - 116) / 22 : 0);
    if (row > editor_cells[editor_cell_count].row) row = editor_cells[editor_cell_count].row;
    editor_caret = editor_cells[editor_nearest(row, (x - w->x - 22 + 4) / 8)].byte;
}

static void editor_body(Window *w, int bottom) {
    editor_layout(w);
    int x = w->x + 22, top = w->y + 116, visible = (bottom - top - 20) / 22 + 1;
    int caret = editor_cell_at(editor_caret), anchor = editor_cell_at(editor_anchor);
    int row = editor_cells[caret].row;
    if (row < editor_scroll_row) editor_scroll_row = row;
    if (row >= editor_scroll_row + visible) editor_scroll_row = row - visible + 1;
    int low = caret < anchor ? caret : anchor, high = caret > anchor ? caret : anchor;
    for (int i = 0; i <= editor_cell_count; i++) {
        EditorCell cell = editor_cells[i];
        if (cell.row < editor_scroll_row) continue;
        int py = top + (cell.row - editor_scroll_row) * 22;
        if (py + 20 > bottom) break;
        int px = x + cell.column * 8;
        if (i >= low && i < high) {
            int span = editor_cells[i + 1].row == cell.row
                       ? (editor_cells[i + 1].column - cell.column) * 8 : glyph_width(cell.cp);
            rect(px, py, span > 0 ? span : 8, 20, selected_fill);
        }
        if (cell.cp && cell.cp != '\r' && cell.cp != '\n' && cell.cp != '\t' &&
            (cell.cp != 0xfeff || cell.byte)) {
            char scalar[5];
            unsigned size = may_utf8_put(scalar, cell.cp);
            scalar[size] = 0;
            text(px, py, scalar, ink);
        }
        if (i == caret && focus == 3 && ticks % 100 < 55) rect(px, py + 2, 2, 18, accent);
    }
}

static void editor_draw(Window *w) {
    int x = w->x, y = w->y;
    MayVfsNode node;
    int open = may_vfs_info(editor_file, &node);
    if (open && node.read_only)
        text(x + 29, y + 61, L(READ_ONLY), muted);
    else
        button(x + 17, y + 55, 90, L(SAVE), 1);
    const char *name = open ? node.name : "没有打开文件";
    const char *at = name, *end = name;
    int name_width = w->w - 392;
    while (*at) {
        name_width -= glyph_width(may_utf8_next(&at));
        if (name_width < 0) break;
        end = at;
    }
    char label[64];
    u32 name_size = (u32)(end - name);
    memcpy(label, name, name_size);
    label[name_size] = 0;
    int right = text(x + 126, y + 61, label, ink);
    if (*end) text(right, y + 61, "…", muted);
    if (editor_changed)
        text(x + w->w - 110, y + 61, "● 未保存", accent);
    int writable = open && !node.read_only;
    if (writable) {
        int left = editor_ime_left(w);
        button(left, y + 55, EDIT_IME_WIDTH, ime_names[editor_ime.mode], 0);
        text(left + 91, y + 60, "F3", muted);
    }
    rect(x + 17, y + 99, w->w - 34, 1, line);
    int composing = writable && editor_ime.size;
    int bottom = composing ? editor_ime_top(w) - 10 : y + w->h - 42;
    editor_body(w, bottom);
    if (composing) ime_draw(&editor_ime, x + 20, editor_ime_top(w), w->w - 40);
    rect(x + 17, y + w->h - 36, w->w - 34, 1, line);
    text(x + 20, y + w->h - 29, "纯文本 · UTF-8", muted);
    numtext(x + w->w - 140, y + w->h - 29, strlen(editor_buf), muted);
    char limit[24] = "/ ";
    number(limit + 2, fs_file_limit());
    copy(limit + strlen(limit), " B", sizeof(limit) - strlen(limit));
    text(x + w->w - 89, y + w->h - 29, limit, muted);
}
/* Explorer shares its interface across both kernels. */
static char explorer_query[72], explorer_bookmark[72];
static int explorer_scroll;
static const char explorer_home[] = "https://example.com/";
enum { EXPLORER_PAGE_Y = 143, EXPLORER_FOOTER = 38, EXPLORER_ROW = 21 };

/* Advance by decoded characters so a wrapped line never splits UTF-8. */
static const char *explorer_line(const char *line, int width, const char **end) {
    const char *next = line;
    while (*next && *next != '\n') {
        const char *glyph = next;
        width -= glyph_width(may_utf8_next(&next));
        if (width < 0) {
            next = glyph;
            break;
        }
    }
    *end = next;
    return *next == '\n' ? next + 1 : next;
}

static int explorer_visible_rows(const Window *w) {
    return (w->h - EXPLORER_PAGE_Y - EXPLORER_FOOTER - 20) / EXPLORER_ROW;
}
static ExplorerHistory explorer_history;
static char explorer_response[4096];
static void explorer_submit(void) {
    if (!explorer_query[0]) copy(explorer_query, explorer_home, sizeof(explorer_query));
    if (remote_loading) {
        notify(pointer_label("正在检查更新，请稍候", "Update check in progress. Try again shortly."));
        return;
    }
    explorer_loading = platform_web_start(explorer_query);
    explorer_scroll = 0;
    if (explorer_loading) explorer_history_push(&explorer_history, explorer_query);
    platform_web_poll(explorer_response, sizeof(explorer_response));
    dirty = 1;
}
/* The Explorer toolbar and application menu share the same history path. */
static void explorer_navigate(int direction) {
    if (remote_loading) {
        notify(pointer_label("正在检查更新，请稍候", "Update check in progress. Try again shortly."));
        return;
    }
    if (!explorer_history.count) return;
    const char *url = direction ? explorer_history_move(&explorer_history, direction)
                               : explorer_history.urls[explorer_history.position];
    if (!url) return;
    copy(explorer_query, url, sizeof(explorer_query));
    explorer_loading = platform_web_start(explorer_query);
    explorer_scroll = 0;
    platform_web_poll(explorer_response, sizeof(explorer_response));
}
static void explorer_save_bookmark(void) {
    copy(explorer_bookmark, explorer_query, sizeof(explorer_bookmark));
    notify(explorer_query[0] ? "已收藏到本次会话" : "请先输入搜索或链接");
}

static void explorer_draw(Window *w) {
    int x = w->x, y = w->y;
    button(x + 18, y + 51, 34, "<", 0);
    button(x + 57, y + 51, 34, ">", 0);
    ui_card(x + 96, y + 51, 34, 30, 10, surface_high);
    ring(x + 112, y + 66, 7, 2, ink);
    rect(x + 113, y + 58, 9, 7, surface_high);
    stroke(x + 116, y + 59, x + 120, y + 64, 1, ink);
    stroke(x + 114, y + 64, x + 120, y + 64, 1, ink);
    ui_card(x + 140, y + 49, w->w - 240, 34, 11, bg);
    const char *address = explorer_query[0] ? explorer_query : explorer_home;
    while (*address && text_width(address) > w->w - 265) may_utf8_next(&address);
    text(x + 152, y + 56, address, explorer_query[0] ? ink : muted);
    button(x + w->w - 90, y + 51, 72,
           explorer_loading ? pointer_label("停止", "Stop") : pointer_label("转到", "Go"), !explorer_loading);
    const char *status = explorer_loading ? pointer_label("正在加载…", "Loading…") :
        pointer_label("HTTP(S) 文本阅读器", "HTTP(S) text reader");
    text(x + 21, y + 98, status, muted);
    if (explorer_bookmark[0])
        text(x + 215, y + 98, explorer_bookmark, accent);
    ui_card(x + 18, y + EXPLORER_PAGE_Y, w->w - 36,
            w->h - EXPLORER_PAGE_Y - EXPLORER_FOOTER, 15, bg);
    const char *s = explorer_response;
    if (!*s) s = pointer_label("输入网页地址，按回车打开。", "Enter a web address and press Enter.");
    const char *end;
    for (int row = 0; row < explorer_scroll && *s; row++)
        s = explorer_line(s, w->w - 74, &end);
    for (int row = 0; row < explorer_visible_rows(w) && *s; row++) {
        char part[W / 2 + 1];
        const char *next = explorer_line(s, w->w - 74, &end);
        u32 length = (u32)(end - s);
        memcpy(part, s, length);
        part[length] = 0;
        text(x + 36, y + EXPLORER_PAGE_Y + 12 + row * EXPLORER_ROW, part, ink);
        s = next;
    }
    text(x + 22, y + w->h - 28,
         pointer_label("↑ ↓ 滚动", "↑ ↓ Scroll"), muted);
}
static int dock_layout(int items[DOCK_SLOT_COUNT]) {
    int count = 0;
    for (int i = DOCK_LAUNCHER; i <= DOCK_TERMINAL; i++) items[count++] = i;
    for (int id = 3; id < WINDOW_COUNT; id++)
        if (windows[id].open) items[count++] = id + 1;
#ifdef MAYOS_LINUX
    if (platform_browser_ready()) items[count++] = DOCK_FIREFOX;
#endif
    items[count++] = DOCK_TRASH;
    return count;
}

static int dock_hit(int x, int y) {
    if (y < DOCK_TOP || y >= DOCK_TOP + DOCK_HEIGHT) return -1;
    int items[DOCK_SLOT_COUNT], count = dock_layout(items);
    for (int slot = 0; slot < count; slot++) {
        int left = dock_slot_x(slot, count);
        if (x >= left - 3 && x < left + 54) return items[slot];
    }
    return -1;
}

static int dock_popup_left(int item) {
    int items[DOCK_SLOT_COUNT], count = dock_layout(items);
    for (int slot = 0; slot < count; slot++)
        if (items[slot] == item) return dock_slot_x(slot, count) + 25 - 91;
    return -1;
}

static void taskbar(void) {
    int items[DOCK_SLOT_COUNT], count = dock_layout(items);
    int width = 16 + count * 57 + 14, left = (W - width) / 2, top = DOCK_TOP;
#ifndef MAYOS_LINUX
    soft_roundrect(left + 3, top + 8, width, DOCK_HEIGHT, 25, dark ? 0x010102 : 0xc0c0c6);
#endif
    ui_card(left, top, width, DOCK_HEIGHT, 24, dark ? 0x0e0e11 : 0xf7f7f9);
    const int kinds[] = {6, 0, 1, 2, 5, 4, 11, 3, 4};
    const char *const labels[] = {pointer_label("应用启动台", "Launchpad"), L(FILES),
        L(SETTINGS), L(TERMINAL), L(EDITOR), "MayOS Explorer", "MayOS Task Manager", L(TRASH), "Firefox"};
    int hovered = modal || egg_open || dock_popup >= 0 ? -1 : dock_hit(mx, my);
    for (int slot = 0; slot < count; slot++) {
        int i = items[slot], x = dock_slot_x(slot, count);
        int window = (i == DOCK_TRASH || i == DOCK_FIREFOX) ? 0 : i - 1;
        int open = i == DOCK_LAUNCHER ? start_open :
            windows[window].open && (window != 0 || (folder == 3) == (i == DOCK_TRASH));
        int active = open && (i == DOCK_LAUNCHER || (focus == window && !windows[window].min));
#ifdef MAYOS_LINUX
        if (i == DOCK_FIREFOX) {
            int state = platform_browser_state();
            open = state != 0;
            active = state == 2;
        }
#endif
        if (i == DOCK_TRASH) rect(x - 11, top + 15, 1, 39, line);
        if (active || i == hovered)
            soft_roundrect(x, top + 5, 50, 54, 15, active ? selected_fill : surface_high);
        int size = 34 + dock_level[i] * 6 / 256;
        symbol(x + (50 - size) / 2, top + 15 - (size - 34), kinds[i], size, active ? ink : muted);
        if (open) circle(x + 25, top + 62, 2, active ? ink : muted);
        if (i == hovered && !start_open) {
            const char *label = labels[i];
            int label_width = text_width(label) + 24;
            int lx = x + 25 - label_width / 2;
            ui_card(lx, top - 38, label_width, 28, 9, panel);
            center_text(lx, label_width, top - 34, label, ink);
        }
    }
    int popup = dock_popup_left(dock_popup);
    if (popup >= 0) {
        int id = dock_popup == DOCK_FIREFOX ? 0 : dock_popup - 1;
        ui_card(popup, top - 108, 182, 97, 12, panel);
        const char *const rows[] = {pointer_label("打开或恢复", "Open or Restore"),
            pointer_label("最小化", "Minimize"), pointer_label("关闭窗口", "Close Window")};
        for (int row = 0; row < 3; row++) {
            int y = top - 104 + row * 30;
            int enabled = row == 0 || (windows[id].open && (row != 1 || !windows[id].min));
#ifdef MAYOS_LINUX
            if (dock_popup == DOCK_FIREFOX)
                enabled = row == 0 || (platform_browser_state() && row == 2) ||
                          (row == 1 && platform_browser_state());
#endif
            if (enabled && inside(mx, my, popup + 4, y, 174, 29))
                soft_roundrect(popup + 4, y, 174, 29, 6, selected_fill);
            text(popup + 12, y + 4, rows[row], enabled ? ink : muted);
        }
    }
}
/* Functional, keyboard-dismissable popovers are deliberately invisible
   until the user clicks the corresponding screenshot-matched status glyph. */
static void status_panel_draw(void) {
    if (!top_panel) return;
    int x = STATUS_PANEL_X, y = 51, w = 318, h = 235;
#ifdef MAYOS_LINUX
    ui_card(x, y, w, h, WINDOW_RADIUS, panel);
#else
    roundrect(x + 4, y + 5, w, h, dark ? 0x111115 : 0xd4d2d7);
    roundrect(x, y, w, h, panel);
#endif
    rect(x + 18, y + 50, w - 36, 1, line);
    const char *names[] = {"", "会话时钟", "网络状态", "显示与外观", "通知中心", "电源"};
    text(x + 21, y + 16, names[top_panel], ink);
    text(x + w - 36, y + 16, "×", muted);
    if (top_panel == 1) {
        text(x + 21, y + 76, "当前会话运行时间", muted);
        numtext(x + 21, y + 115, ticks / 100, ink);
        text(x + 105, y + 115, "秒（非真实日期时钟）", muted);
        text(x + 21, y + 169, "系统信息可在设置中查看", muted);
        button(x + 19, y + 193, 125, "打开设置", 0);
    } else if (top_panel == 2) {
        text(x + 21, y + 74, "网络连接信息", ink);
#ifdef MAYOS_LINUX
        char state[180] = {0};
        int online = platform_network_status(state, sizeof(state));
        text(x + 21, y + 113, online > 0 ? "Linux 网络后端已响应" :
                                      "网络不可用或尚未就绪", muted);
#else
        text(x + 21, y + 113, "MayKernel: 网络能力有限", muted);
#endif
        button(x + 19, y + 193, 145, browser_button(), 0);
    } else if (top_panel == 3) {
        text(x + 21, y + 74, "系统主题", ink);
        button(x + 19, y + 108, 126, "深色", dark);
        button(x + 161, y + 108, 126, "浅色", !dark);
        text(x + 21, y + 160, "主题变化会按现有设置规则保存", muted);
        button(x + 19, y + 193, 145, "更多外观设置", 0);
    } else if (top_panel == 4) {
        text(x + 21, y + 79, notice_until > ticks ? notice : "暂无新通知", ink);
        text(x + 21, y + 132, "应用和系统提示会显示在此处", muted);
        button(x + 19, y + 193, 125, "清除提示", 0);
    } else {
        text(x + 21, y + 76, "结束当前 MayOS 会话", muted);
        button(x + 19, y + 117, 126, "重新启动", 0);
        button(x + 161, y + 117, 126, "关闭系统", 0);
        text(x + 21, y + 181, "操作前会同步可写入的 MayFS 数据", muted);
    }
}
static unsigned char lower_ascii(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static int contains(const char *a, const char *b) {
    if (!*b) return 1;
    for (; *a; a++) {
        int i = 0;
        while (b[i] && a[i] && lower_ascii(a[i]) == lower_ascii(b[i])) i++;
        if (!b[i]) return 1;
    }
    return 0;
}

enum AppId { APP_FILES, APP_SETTINGS, APP_TERMINAL, APP_EDITOR, APP_EXPLORER, APP_TRASH, APP_TASK, APP_GAME, APP_FIREFOX };
typedef struct {
    enum AppId id;
    int icon;
    const char *name, *keywords;
} AppEntry;

static int apps_list(AppEntry *result) {
    const AppEntry apps[] = {
        {APP_FILES, 0, L(FILES), "files documents 文件"},
        {APP_SETTINGS, 1, L(SETTINGS), "settings 设置"},
        {APP_TERMINAL, 2, L(TERMINAL), "terminal shell 终端"},
        {APP_EDITOR, 5, L(EDITOR), "editor notes text 编辑器"},
        {APP_EXPLORER, 4, "MayOS Explorer", "browser explorer 浏览器"},
#ifdef MAYOS_LINUX
        {APP_FIREFOX, 4, "Firefox", "firefox browser 浏览器"},
#endif
#ifdef MAYOS_TASK_MANAGER
        {APP_TASK, 11, "MayOS Task Manager", "taskmgr task manager 任务管理器"},
#endif
#ifndef MAYOS_LINUX
        {APP_GAME, 20, "夏日未寄", "game summer 游戏"},
#endif
        {APP_TRASH, 3, L(TRASH), "trash recycle 回收站"}
    };
    int count = 0;
    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        AppEntry entry = apps[i];
#ifdef MAYOS_LINUX
        if (entry.id == APP_FIREFOX && !platform_browser_ready()) continue;
        if (entry.id == APP_EXPLORER && platform_browser_ready()) continue;
#endif
        if (contains(entry.keywords, search) || contains(entry.name, search)) result[count++] = entry;
    }
    return count;
}

static int launcher_row;
static void toggle_launcher(void) {
    start_open = !start_open;
    active_menu = -1;
    context_open = top_panel = 0;
    search[0] = 0;
    launcher_row = 0;
    if (start_open) serial("MayOS: application launcher opened\n");
}
static void launch_app(enum AppId id) {
    start_open = 0;
    switch (id) {
    case APP_FILES:
        if (folder == 3) {
            folder = 1;
            file_scroll = 0;
            selected = -1;
        }
        raise_window(0);
        break;
    case APP_SETTINGS: open_settings(SETTINGS_GENERAL); break;
    case APP_TERMINAL: raise_window(2); break;
    case APP_EDITOR: {
        MayVfsNode file;
        if (may_vfs_info(editor_file, &file) && !file.directory) raise_window(3);
        else {
            int notes = fs_find(2, "Notes.txt");
            if (notes >= 0) open_file(notes);
            else {
                folder = 1;
                file_scroll = 0;
                selected = -1;
                raise_window(0);
                begin_modal(1, -1);
            }
        }
        break;
    }
    case APP_EXPLORER:
#ifdef MAYOS_LINUX
        if (platform_browser_ready()) {
            launch_app(APP_FIREFOX);
            break;
        }
#endif
        raise_window(4);
        break;
    case APP_FIREFOX:
#ifdef MAYOS_LINUX
        if (!platform_browser_open()) notify(pointer_label("Firefox 启动失败", "Could not launch Firefox"));
#endif
        break;
    case APP_TRASH:
        folder = 3;
        file_scroll = 0;
        selected = -1;
        raise_window(0);
        serial("MayOS: trash opened\n");
        break;
    case APP_TASK:
#ifdef MAYOS_TASK_MANAGER
        task_manager_open();
#endif
        break;
    case APP_GAME:
#ifndef MAYOS_LINUX
        maygame_open();
#endif
        break;
    }
    serial("MayOS: launcher application opened\n");
}

#define LAUNCHER_WIDTH 856
#define LAUNCHER_HEIGHT 390
#define LAUNCHER_X ((W - LAUNCHER_WIDTH) / 2)
#define LAUNCHER_Y 112
#define LAUNCHER_COLUMNS 4
#define LAUNCHER_TILE_WIDTH 194
#define LAUNCHER_TILE_HEIGHT 94
static int launcher_tile_x(int index) { return LAUNCHER_X + 25 + index % LAUNCHER_COLUMNS * 203; }
static int launcher_tile_y(int index) { return LAUNCHER_Y + 128 + index / LAUNCHER_COLUMNS * 106; }
static void start_draw(void) {
    int x = LAUNCHER_X, y = LAUNCHER_Y;
#ifndef MAYOS_LINUX
    soft_roundrect(x + 5, y + 8, LAUNCHER_WIDTH, LAUNCHER_HEIGHT, 24, dark ? 0x020203 : 0xc8c8cb);
#endif
    ui_card(x, y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT, 24, panel);
    heading_text(x + 25, y + 18, pointer_label("应用启动台", "Application launcher"), ink);
    symbol(x + LAUNCHER_WIDTH - 42, y + 20, 14, 21, muted);
    ui_card(x + 25, y + 63, LAUNCHER_WIDTH - 50, 40, 14, bg);
    symbol(x + 39, y + 72, 19, 20, muted);
    text(x + 68, y + 73, search[0] ? search : pointer_label("搜索应用", "Search applications"),
         search[0] ? ink : muted);
    AppEntry apps[8];
    int count = apps_list(apps);
    for (int i = 0; i < count; i++) {
        int xx = launcher_tile_x(i), yy = launcher_tile_y(i);
        int selected = i == launcher_row || inside(mx, my, xx, yy, LAUNCHER_TILE_WIDTH, LAUNCHER_TILE_HEIGHT);
        ui_card(xx, yy, LAUNCHER_TILE_WIDTH, LAUNCHER_TILE_HEIGHT, 14, selected ? selected_fill : surface_high);
        symbol(xx + (LAUNCHER_TILE_WIDTH - 36) / 2, yy + 11, apps[i].icon, 36, ink);
        center_text(xx, LAUNCHER_TILE_WIDTH, yy + 56, apps[i].name, ink);
    }
    if (!count)
        center_text(x, LAUNCHER_WIDTH, y + 205, pointer_label("没有匹配的应用", "No matching applications"), muted);
}

enum MenuAction {
    ACTION_APP, ACTION_SETTINGS, ACTION_NEW_FILE, ACTION_SAVE, ACTION_FILE_EDIT, ACTION_PASTE, ACTION_IMPORT,
    ACTION_FOLDER, ACTION_FILE_UP, ACTION_EDITOR_KEY, ACTION_SHELL, ACTION_EXPLORER,
    ACTION_THEME, ACTION_SCALE, ACTION_WINDOW, ACTION_MINIMIZE, ACTION_MAXIMIZE, ACTION_CLOSE,
    ACTION_HELP, ACTION_LAUNCHER
};
typedef struct { const char *label; int action, value, enabled; } MenuItem;
enum { MENU_MAX_ITEMS = SETTINGS_TAB_COUNT + WINDOW_COUNT + 3 };
#define MENU_POPUP_Y 34
#define MENU_ROW_HEIGHT 32

static int menu_items(MenuItem *items) {
    int count = 0;
    int file_selected = focus == 0 && selected >= 4 &&
                        selected < FS_MAX && files[selected].used && !may_vfs_read_only(selected);
    int writable_folder = !may_vfs_read_only(folder);
#define ENTRY(label, action, value, enabled) items[count++] = (MenuItem){label, action, value, enabled}
#define ITEM(zh, en, action, value, enabled) ENTRY(pointer_label(zh, en), action, value, enabled)
    switch (active_menu) {
    case MENU_MAYOS:
        ITEM("应用启动台", "Application launcher", ACTION_LAUNCHER, 0, 1);
        ENTRY(L(ABOUT), ACTION_SETTINGS, SETTINGS_ABOUT, 1);
        ENTRY(L(SETTINGS), ACTION_SETTINGS, SETTINGS_GENERAL, 1);
#ifdef MAYOS_TASK_MANAGER
        ENTRY("MayOS Task Manager", ACTION_APP, APP_TASK, 1);
#endif
        break;
    case MENU_APP:
        if (focus < 0) {
            ITEM("应用启动台", "Application launcher", ACTION_LAUNCHER, 0, 1);
            ENTRY(L(FILES), ACTION_APP, APP_FILES, 1);
            ENTRY(L(TERMINAL), ACTION_APP, APP_TERMINAL, 1);
        } else {
            ITEM("最小化", "Minimize", ACTION_MINIMIZE, 0, 1);
            ITEM("关闭窗口", "Close window", ACTION_CLOSE, 0, 1);
        }
        break;
    case MENU_FILE:
        if (focus == 0) {
            ITEM("新建文本文件", "New text file", ACTION_NEW_FILE, 1, writable_folder);
            ITEM("新建文件夹", "New folder", ACTION_NEW_FILE, 2, writable_folder);
            ITEM("打开所选项目", "Open selected item", ACTION_FILE_EDIT, 0, selected >= 0);
            ITEM("属性", "Properties", ACTION_FILE_EDIT, 8, file_selected);
            ITEM("导入文件包", "Import file pack", ACTION_IMPORT, 0, writable_folder && folder != 3);
        } else {
            ITEM("保存文稿", "Save document", ACTION_SAVE, 0, editor_writable());
            ITEM("新建文本文件", "New text file", ACTION_NEW_FILE, 1, !editor_changed);
        }
        break;
    case MENU_EDIT:
        if (focus == 3) {
            int selection = editor_caret != editor_anchor;
            int write = editor_writable() && !editor_ime.size;
            ITEM("撤销", "Undo", ACTION_EDITOR_KEY, MAYOS_KEY_UNDO, write && editor_undo_count);
            ITEM("重做", "Redo", ACTION_EDITOR_KEY, MAYOS_KEY_REDO, write && editor_redo_count);
            ITEM("剪切", "Cut", ACTION_EDITOR_KEY, MAYOS_KEY_CUT, write && selection);
            ITEM("复制", "Copy", ACTION_EDITOR_KEY, MAYOS_KEY_COPY, selection);
            ITEM("粘贴", "Paste", ACTION_EDITOR_KEY, MAYOS_KEY_PASTE, write && editor_clipboard[0]);
            ITEM("全选", "Select all", ACTION_EDITOR_KEY, MAYOS_KEY_SELECT_ALL, editor_buf[0]);
            break;
        }
        ITEM("复制所选文件", "Copy selected file", ACTION_FILE_EDIT, 4,
             file_selected);
        ITEM("剪切所选项目", "Cut selected item", ACTION_FILE_EDIT, 6, file_selected && writable_folder);
        ITEM("粘贴到当前文件夹", "Paste into folder", ACTION_PASTE, 0,
             clip >= 4 && clip < FS_MAX && files[clip].used && writable_folder);
        ITEM("重命名所选项目", "Rename selected item", ACTION_FILE_EDIT, 3,
             file_selected && writable_folder);
        break;
    case MENU_GO:
        ITEM("上一级", "Enclosing folder", ACTION_FILE_UP, 0, folder != 0);
        ENTRY(L(DOCUMENTS), ACTION_FOLDER, 1, 1);
        ENTRY(L(DESKTOP), ACTION_FOLDER, 2, 1);
        ENTRY(L(TRASH), ACTION_FOLDER, 3, 1);
        break;
    case MENU_SECTIONS:
        for (int i = 0; i < SETTINGS_TAB_COUNT; i++)
            ENTRY(settings_label(i), ACTION_SETTINGS, i, 1);
        break;
    case MENU_INPUT:
        ITEM("切换输入法", "Switch input method", ACTION_EDITOR_KEY, MAYOS_KEY_IME, editor_writable());
        ITEM("确认当前输入", "Commit composition", ACTION_EDITOR_KEY, '\n', editor_writable() && editor_ime.size);
        break;
    case MENU_SHELL:
        ITEM("清空屏幕", "Clear screen", ACTION_SHELL, 0, out_count > 0);
        ITEM("调用上一条命令", "Recall last command", ACTION_SHELL, 1, history[0]);
        ITEM("清空当前输入", "Clear input", ACTION_SHELL, 2, cmdlen > 0);
        break;
    case MENU_NAVIGATE:
        ITEM("后退", "Back", ACTION_EXPLORER, -1, explorer_history.position > 0);
        ITEM("前进", "Forward", ACTION_EXPLORER, 1,
             explorer_history.position + 1 < explorer_history.count);
        ITEM("重新载入", "Reload", ACTION_EXPLORER, 0, explorer_history.count > 0);
        ITEM("收藏地址", "Bookmark address", ACTION_EXPLORER, 2, explorer_query[0]);
        ITEM("使用收藏地址", "Use bookmarked address", ACTION_EXPLORER, 3, explorer_bookmark[0]);
        ITEM("清空地址", "Clear address", ACTION_EXPLORER, 4, explorer_query[0]);
        break;
    case MENU_VIEW:
        ENTRY(L(DISPLAY), ACTION_SETTINGS, SETTINGS_DISPLAY, 1);
        ENTRY(dark ? pointer_label("切换浅色主题", "Use light appearance") :
                     pointer_label("切换深色主题", "Use dark appearance"), ACTION_THEME, 0, 1);
        ITEM("恢复自动缩放", "Use automatic scaling", ACTION_SCALE, 0, 1);
        break;
    case MENU_WINDOW:
        if (focus >= 0) {
            ITEM("最小化当前窗口", "Minimize current window", ACTION_MINIMIZE, 0, 1);
            ENTRY(windows[focus].max ? pointer_label("还原当前窗口", "Restore current window") :
                  pointer_label("最大化当前窗口", "Maximize current window"), ACTION_MAXIMIZE, 0, 1);
            ITEM("关闭当前窗口", "Close current window", ACTION_CLOSE, 0, 1);
        }
        for (int i = WINDOW_COUNT - 1; i >= 0; i--) {
            int id = order[i];
            if (windows[id].open && id != focus)
                ENTRY(window_menu_name(id), ACTION_WINDOW, id, 1);
        }
        break;
    case MENU_HELP:
        ITEM("快捷键与命令", "Shortcuts and commands", ACTION_HELP, 0, 1);
        ENTRY(settings_label(SETTINGS_CREDITS), ACTION_SETTINGS, SETTINGS_CREDITS, 1);
        break;
    default: break;
    }
#undef ENTRY
#undef ITEM
    return count;
}

static int menu_popup_width(const MenuItem *items, int count) {
    int width = 218;
    for (int i = 0; i < count; i++) {
        int needed = text_width(items[i].label) + 30;
        if (needed > width) width = needed;
    }
    return width;
}

static int menu_popup_x(int width) {
    int x = menu_left[active_menu];
    return x + width < W ? x : W - width - 5;
}

static void menu_draw(void) {
    if (active_menu < 0) return;
    MenuItem items[MENU_MAX_ITEMS];
    int count = menu_items(items);
    if (!count) return;
    int width = menu_popup_width(items, count), x = menu_popup_x(width);
    int height = 12 + count * MENU_ROW_HEIGHT;
#ifndef MAYOS_LINUX
    soft_roundrect(x + 3, MENU_POPUP_Y + 5, width, height, 13, dark ? 0x030304 : 0xd0d1d4);
#endif
    ui_card(x, MENU_POPUP_Y, width, height, 13, panel);
    for (int i = 0; i < count; i++) {
        int y = MENU_POPUP_Y + 6 + i * MENU_ROW_HEIGHT;
        int selected = menu_row == i || inside(mx, my, x + 5, y, width - 10, MENU_ROW_HEIGHT);
        if (items[i].enabled && selected) soft_roundrect(x + 5, y, width - 10, MENU_ROW_HEIGHT, 8, selected_fill);
        text(x + 15, y + 5, items[i].label, items[i].enabled ? ink : muted);
    }
}
static void context_draw(void) {
#ifdef MAYOS_LINUX
    ui_card(ctxx, ctxy, 184, 320, WINDOW_RADIUS, panel);
#else
    roundrect(ctxx + 3, ctxy + 3, 184, 320, dark ? 0x141218 : 0xd8d1df);
    roundrect(ctxx, ctxy, 184, 320, panel);
#endif
    const char *items[] = {"打开", "新建文件", "新建文件夹",        "重命名", "复制",
                           "粘贴", "剪切",     "移到回收站 / 删除", "属性"};
    for (int i = 0; i < 9; i++)
        text(ctxx + 15, ctxy + 9 + i * 34, items[i], i == 7 ? 0xda7373 : ink);
}
static void modal_draw(void) {
    if (modal == MODAL_POWER) {
        int x = 358, y = 256;
        ui_card(x, y, 564, 208, 19, panel);
        heading_text(x + 24, y + 18,
                     modal_target == POWER_RESTART ? pointer_label("确认重新启动", "Restart MayOS?") :
                                                     pointer_label("确认关机", "Shut down MayOS?"), ink);
        const char *message = power_risk == POWER_EDITOR_CHANGES ?
            pointer_label("编辑器中有未保存的内容。", "The editor has unsaved changes.") :
            power_risk == POWER_WRITE_FAILED ?
            pointer_label("写入磁盘失败，修改仍在内存中。", "Disk write failed. Changes are still in memory.") :
            pointer_label("文件修改仅保存在当前会话中。", "File changes are saved for this session only.");
        text(x + 24, y + 66, message, ink);
        text(x + 24, y + 99,
             pointer_label("继续操作会丢失这些修改。", "Continuing will discard these changes."), muted);
        button(552, 408, 124, L(CANCEL), 1);
        button(692, 408, 206,
               modal_target == POWER_RESTART ? pointer_label("放弃修改并重启", "Discard and Restart") :
                                               pointer_label("放弃修改并关机", "Discard and Shut Down"), 0);
        return;
    }
    int x = 308, y = 262;
#ifdef MAYOS_LINUX
    ui_card(x, y, 408, 202, WINDOW_RADIUS, panel);
#else
    roundrect(x + 5, y + 6, 408, 202, dark ? 0x141218 : 0xd8d1df);
    roundrect(x, y, 408, 202, panel);
#endif
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
        text(x + 23, y + 52, "名称（UTF-8，最多 47 字节）", muted);
        button(x + 266, y + 46, 120, ime_names[modal_ime.mode], 0);
        text(x + 356, y + 50, "F3", muted);
        roundrect(x + 22, y + 83, 364, 36, bg);
        const char *visible = modal_text;
        while (*visible && text_width(visible) > 335) may_utf8_next(&visible);
        int end = text(x + 33, y + 91, visible, ink);
        rect(end + 1, y + 91, 1, 20, accent);
        if (modal_ime.size) ime_draw(&modal_ime, x, y + 212, 408);
    }
    button(x + 204, y + 151, 80, "取消", 0);
    button(x + 296, y + 151, 90, "确定", 1);
}
static void easter_pixels(const u16 *image, int width, int height, int x, int y) {
    for (int row = 0; row < height; row++)
        for (int col = 0; col < width; col++) {
            u16 color = image[row * width + col];
            u32 red = color >> 11, green = (color >> 5) & 63, blue = color & 31;
            pixels[(y + row) * W + x + col] =
                ((red << 3 | red >> 2) << 16) |
                ((green << 2 | green >> 4) << 8) | (blue << 3 | blue >> 2);
        }
}

enum { ART_X = 164, ART_Y = 53, ART_WIDTH = 952, ART_HEIGHT = 614,
       ART_IMAGE_TOP = 96, ART_IMAGE_HEIGHT = 520 };
/* Kept outside the normal window list: hidden until explicitly discovered. */
static void easter_draw(void) {
    if (egg_open == EGG_TENSHI) {
        roundrect(ART_X, ART_Y, ART_WIDTH, ART_HEIGHT, surface_high);
        roundrect(ART_X + 1, ART_Y + 1, ART_WIDTH - 2, ART_HEIGHT - 2, panel);
        text_face(ART_X + 24, ART_Y + 15, "超絶最かわてんしちゃん", ink, MAY_JA);
        text(ART_X + ART_WIDTH - 34, ART_Y + 13, "×", muted);
        roundrect(ART_X + 20, ART_IMAGE_TOP, ART_WIDTH - 40, ART_IMAGE_HEIGHT, 0x000000);
        easter_pixels(tenshi_rgb565, TENSHI_W, TENSHI_H, (W - TENSHI_W) / 2,
                      ART_IMAGE_TOP + (ART_IMAGE_HEIGHT - TENSHI_H) / 2);
        text(ART_X + 24, ART_Y + ART_HEIGHT - 32, pointer_label("按 Esc 返回", "Esc to return"), muted);
        button(ART_X + ART_WIDTH - 128, ART_Y + ART_HEIGHT - 42, 108,
               pointer_label("返回桌面", "Return"), 1);
        return;
    }
#ifdef MAYOS_LINUX
    ui_card(282, 66, 460, 626, WINDOW_RADIUS, panel);
#else
    roundrect(288, 72, 460, 626, dark ? 0x101a29 : 0xb9c8d7);
    roundrect(282, 66, 460, 626, panel);
#endif
    text(306, 85, "喵！你发现了一位小住客", ink);
    text(710, 83, "×", muted);
    easter_pixels(cat_rgb565, CAT_W, CAT_H, (W - CAT_W) / 2, 124);
    text(306, 623, "喵，原来你在这里。", ink);
    text(306, 653, "按 Esc 返回", muted);
    button(610, 646, 108, "返回桌面", 1);
}
static void draw_pointer_frame(const MayCursorFrame *frame, int x, int y, int side) {
    if (!frame->width || !frame->height) return;
    int edge = frame->width > frame->height ? frame->width : frame->height;
    int width = (frame->width * side + edge - 1) / edge;
    int height = (frame->height * side + edge - 1) / edge;
    int left = x - frame->hotspot_x * width / frame->width;
    int top = y - frame->hotspot_y * height / frame->height;
    for (int row = 0; row < height; row++)
        for (int col = 0; col < width; col++) {
            int px = left + col, py = top + row;
            if (px < 0 || px >= W || py < 0 || py >= H) continue;
            u32 color = frame->pixels[(row * frame->height / height) * frame->width +
                                      col * frame->width / width];
            u32 alpha = color >> 24, rgb = color & 0xffffffu;
            if (alpha == 255) rect(px, py, 1, 1, rgb);
            else if (alpha) rect(px, py, 1, 1, blend(pixels[py * W + px], rgb, alpha));
            else if (rgb) rect(px, py, 1, 1, pixels[py * W + px] ^ rgb);
        }
}

static void cursor(void) {
    int side = 12 + pointer_size * 6;
    if (cursor_style == POINTER_CUSTOM && custom_cursor.count) {
        u32 frame = may_cursor_frame(&custom_cursor, ticks - cursor_started);
        draw_pointer_frame(&custom_cursor.frames[frame], mx, my,
                           cursor_active_source == POINTER_PPM ? side : 24 + pointer_size * 8);
        return;
    }
    may_draw_system_pointer(pixels, mx, my, 21 + pointer_size * 6,
                            cursor_style == POINTER_DARK);
}

static void render(void) {
    palette();
    desktop();
    for (int k = 0; k < WINDOW_COUNT; k++) {
        int i = order[k];
        Window *w = &windows[i];
        if (!w->open || w->min)
            continue;
        u32 elapsed = ticks - animation_started[i];
        if (high_contrast || elapsed >= 18) animation_running[i] = 0;
        int fading = animation_running[i];
        if (fading) memcpy(animation_underlay, pixels, sizeof(pixels));
        frame(i);
#ifdef MAYOS_LINUX
        u32 corners[2][WINDOW_INNER_RADIUS][WINDOW_INNER_RADIUS];
        window_bottom_corners(w, corners, 0);
#endif
#ifndef MAYOS_LINUX
        if (i == 1) {
#endif
            draw_clip_active = 1;
            draw_clip_left = w->x + 1;
            draw_clip_top = w->y + (i == 1 ? 36 : 43);
            draw_clip_right = w->x + w->w - 1;
            draw_clip_bottom = w->y + w->h - 1;
#ifndef MAYOS_LINUX
        }
#endif
        if (i == 0)
            files_draw(w);
        else if (i == 1)
            settings(w);
        else if (i == 2)
            terminal_draw(w);
        else if (i == 3)
            editor_draw(w);
#ifdef MAYOS_TASK_MANAGER
        else if (i == TASK_WINDOW)
            may_task_draw(w->x + 1, w->y + 43, w->w - 2, w->h - 44, dark);
#endif
        else
            explorer_draw(w);
        draw_clip_active = 0;
#ifdef MAYOS_LINUX
        window_bottom_corners(w, corners, 1);
#endif
        if (fading) {
            u32 remaining = 18 - elapsed;
            u32 alpha = 255 - remaining * remaining * 255 / (18 * 18);
            int offset = (int)(remaining * 7 / 18);
            int left = w->x - 3, right = w->x + w->w + 14;
            int top = w->y - 3, bottom = w->y + w->h + 18 + offset;
            if (left < 0) left = 0;
            if (right > W) right = W;
            if (top < 0) top = 0;
            if (bottom > H) bottom = H;
            /* Ease upward by seven pixels while fading in.  Iterate bottom
               to top so the shifted source is never overwritten early. */
            for (int y = bottom - 1; y >= top; y--)
                for (int x = left; x < right; x++) {
                    int destination = y * W + x;
                    int source_y = y - offset;
                    u32 color = animation_underlay[destination];
                    if (source_y >= top) {
                        int source = source_y * W + x;
                        if (pixels[source] != animation_underlay[source])
                            color = blend(color, pixels[source], alpha);
                    }
                    pixels[destination] = color;
                }
        }
    }
    taskbar();
    status_panel_draw();
    if (start_open)
        start_draw();
    menu_draw();
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
#ifndef MAYOS_LINUX
    if (maygame_active())
        maygame_draw(rect, text, maygame_blit);
#endif
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
        w->h = DOCK_TOP - w->y - 8;
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
    if (may_vfs_read_only(folder)) {
        notify("只读系统目录");
        return;
    }
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
        int i = fs_copy_tree(clip, folder);
        if (i < 0) {
            notify(pointer_label("复制失败：名称冲突、空间不足或目标在源目录内",
                                 "Copy failed: conflict, capacity, or destination inside source"));
            return;
        }
        selected = i;
    }
    sync_notice();
    notify("操作完成");
}
static void trash_selected(void) {
    if (may_vfs_read_only(selected) || may_vfs_read_only(folder)) {
        notify("只读系统文件");
        return;
    }
    if (selected < 4) {
        notify("请先选择普通文件或文件夹");
        return;
    }
    if (editor_file == selected && editor_changed) {
        notify("请先保存正在编辑的文件");
        return;
    }
    if (folder == 3) {
        if (editor_file >= 0 && editor_file < FS_MAX) {
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
                ime_clear(&editor_ime);
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
    if (i != 0 && (may_vfs_read_only(selected) || may_vfs_read_only(folder))) {
        notify("只读系统文件");
        return;
    }
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

static void close_window(int id) {
    if (id == 3 && (editor_changed || editor_ime.size)) {
        notify(pointer_label("请先保存修改，再关闭编辑器", "Save your changes before closing the editor."));
        return;
    }
    windows[id].open = 0;
    if (id == focus) focus_visible_window();
}
static void run_menu_item(MenuItem item) {
    if (!item.enabled) return;
    active_menu = menu_row = -1;
    switch (item.action) {
    case ACTION_APP: launch_app((enum AppId)item.value); break;
    case ACTION_SETTINGS: open_settings(item.value); break;
    case ACTION_NEW_FILE:
        if (focus == 3 && (folder == 3 || may_vfs_read_only(folder))) {
            folder = 1;
            file_scroll = 0;
            selected = -1;
        }
        raise_window(0);
        begin_modal(item.value, -1);
        break;
    case ACTION_SAVE: editor_key(0x106); break;
    case ACTION_FILE_EDIT: context_action(item.value); break;
    case ACTION_PASTE: paste_file(); break;
    case ACTION_IMPORT: {
        u32 size;
        const u8 *pack = platform_import_pack(&size);
        if (!pack) {
            notify(pointer_label("未加载 MayOS 文件包", "No MayOS file pack is loaded"));
            break;
        }
        if (fs_import_pack(folder, pack, size) < 0)
            notify(pointer_label("导入失败：文件包无效、名称重复或剩余空间不足",
                                 "Import failed: invalid pack, conflicting name or insufficient space"));
        else if (sync_notice()) {
            notify(pointer_label("已导入并保存文件", "Imported files saved"));
            serial("MayOS: file pack imported and saved\n");
        }
        break;
    }
    case ACTION_FOLDER: open_file(item.value); break;
    case ACTION_FILE_UP: files_up(); break;
    case ACTION_EDITOR_KEY: editor_key(item.value); break;
    case ACTION_SHELL:
        if (item.value == 0) out_count = scroll = 0;
        else {
            copy(command, item.value == 1 ? history : "", sizeof(command));
            cmdlen = strlen(command);
        }
        break;
    case ACTION_EXPLORER:
        if (item.value == 2) explorer_save_bookmark();
        else if (item.value == 3) {
            copy(explorer_query, explorer_bookmark, sizeof(explorer_query));
            explorer_submit();
        } else if (item.value == 4) explorer_query[0] = 0;
        else explorer_navigate(item.value);
        break;
    case ACTION_THEME: dark = !dark; preferences_save(); break;
    case ACTION_SCALE: display_set_scale_mode(0); preferences_save(); break;
    case ACTION_WINDOW: raise_window(item.value); break;
    case ACTION_MINIMIZE: minimize_window(focus); break;
    case ACTION_MAXIMIZE: toggle_max(focus); break;
    case ACTION_CLOSE: close_window(focus); break;
    case ACTION_HELP:
        raise_window(2);
        emit("Super: Applications  |  F1: Terminal  |  F2: Task Manager");
        emit("F3: Switch input method in editor  |  Esc: Dismiss");
        emit("MayShell: type help to list commands.");
        break;
    case ACTION_LAUNCHER: toggle_launcher(); break;
    }
}
static int menu_click(void) {
    if (my < 32) {
        for (int i = 0; i < MENU_COUNT; i++) {
            if (mx < menu_left[i] || mx >= menu_right[i]) continue;
            active_menu = active_menu == i ? -1 : i;
            menu_row = -1;
            start_open = top_panel = context_open = settings_search_active = 0;
            return 1;
        }
        active_menu = -1;
        return 0;
    }
    if (active_menu < 0) return 0;
    MenuItem items[MENU_MAX_ITEMS];
    int count = menu_items(items), width = menu_popup_width(items, count);
    int x = menu_popup_x(width), top = MENU_POPUP_Y + 6;
    if (inside(mx, my, x, top, width, count * MENU_ROW_HEIGHT))
        run_menu_item(items[(my - top) / MENU_ROW_HEIGHT]);
    else active_menu = menu_row = -1;
    return 1;
}
static void left_click(void) {
    dirty = 1;
#ifndef MAYOS_LINUX
    if (maygame_active()) { maygame_click(mx,my); return; }
#endif
    if (egg_open) {
        if (egg_open == EGG_TENSHI) {
            if (inside(mx, my, ART_X + ART_WIDTH - 46, ART_Y + 5, 40, 36) ||
                inside(mx, my, ART_X + ART_WIDTH - 128, ART_Y + ART_HEIGHT - 42, 108, 30))
                egg_open = EGG_CLOSED;
            return;
        }
        if (inside(mx, my, 696, 76, 38, 34) || inside(mx, my, 610, 646, 108, 30))
            egg_open = EGG_CLOSED;
        return;
    }
    Window *about = &windows[1];
    if (!(about->open && !about->min && focus == 1 && settings_tab == SETTINGS_ABOUT &&
          inside(mx, my, about->x + 282, about->y + 265, 30, 30)))
        egg_clicks = 0;
    if (modal == MODAL_POWER) {
        if (inside(mx, my, 552, 408, 124, 30)) {
            modal = 0;
        } else if (inside(mx, my, 692, 408, 206, 30)) {
            modal = 0;
            run_power_action((enum PowerAction)modal_target);
        }
        return;
    }
    if (modal) {
        if (modal < 5 && inside(mx, my, 574, 308, 120, 30))
            ime_key(&modal_ime, modal_text, 48, MAYOS_KEY_IME);
        else if (modal < 5 && modal_ime.size && inside(mx, my, 321, 521, 381, 35)) {
            unsigned slot = (unsigned)(mx - 321) / 95;
            MayImeChoices choices;
            ime_convert(&modal_ime, display_language == MAY_ZH_HANT, &choices);
            if (choices.count == 1) slot = 0;
            if (slot < IME_PAGE) ime_commit_to(&modal_ime, modal_text, 48, modal_ime.selected / IME_PAGE * IME_PAGE + slot);
        } else if (inside(mx, my, 604, 413, 90, 30)) {
            if (!modal_ime.size) accept_modal();
            else ime_commit_to(&modal_ime, modal_text, 48, modal_ime.selected);
        }
        else if (inside(mx, my, 512, 413, 80, 30)) {
            ime_clear(&modal_ime);
            modal = 0;
        }
        return;
    }
    if (dock_popup >= 0) {
        int item = dock_popup, x = dock_popup_left(dock_popup);
        dock_popup = -1;
        if (x >= 0 && inside(mx, my, x + 4, DOCK_TOP - 104, 174, 90)) {
            int row = (my - (DOCK_TOP - 104)) / 30;
#ifdef MAYOS_LINUX
            if (item == DOCK_FIREFOX) {
                if (row == 0) launch_app(APP_FIREFOX);
                else platform_browser_window_action(row == 2);
                return;
            }
#endif
            int id = item - 1;
            if (row == 0) raise_window(id);
            else if (windows[id].open && row == 1 && !windows[id].min) minimize_window(id);
            else if (windows[id].open && row == 2) close_window(id);
            return;
        }
    }
    int dock_item = dock_hit(mx, my);
    if (dock_item >= 0) {
        active_menu = menu_row = -1;
        context_open = top_panel = 0;
        switch (dock_item) {
        case DOCK_LAUNCHER: toggle_launcher(); break;
        case DOCK_FILES: launch_app(APP_FILES); break;
        case DOCK_SETTINGS: raise_window(1); break;
        case DOCK_TERMINAL: launch_app(APP_TERMINAL); break;
        case DOCK_EDITOR: launch_app(APP_EDITOR); break;
        case DOCK_EXPLORER: launch_app(APP_EXPLORER); break;
        case DOCK_TASK: launch_app(APP_TASK); break;
        case DOCK_TRASH: launch_app(APP_TRASH); break;
        case DOCK_FIREFOX: launch_app(APP_FIREFOX); break;
        }
        return;
    }
    if (menu_click()) return;
    if (context_open) {
        if (inside(mx, my, ctxx, ctxy, 184, 320)) {
            context_action((my - ctxy - 4) / 34);
            return;
        }
        context_open = 0;
    }
    /* A click in an open panel is handled before interacting with windows. */
    if (top_panel && my >= 42) {
        int id = top_panel;
        top_panel = 0;
        if (inside(mx, my, STATUS_PANEL_X, 51, 318, 235)) {
            if (id == 1 && inside(mx, my, STATUS_PANEL_X + 19, 244, 145, 34)) {
                raise_window(1);
            } else if (id == 2 && inside(mx, my, STATUS_PANEL_X + 19, 244, 164, 34)) {
                launch_app(APP_EXPLORER);
            } else if (id == 3) {
                if (inside(mx, my, STATUS_PANEL_X + 19, 159, 126, 34)) {
                    dark = 1; preferences_save();
                } else if (inside(mx, my, STATUS_PANEL_X + 161, 159, 126, 34)) {
                    dark = 0; preferences_save();
                } else if (inside(mx, my, STATUS_PANEL_X + 19, 244, 164, 34)) {
                    raise_window(1);
                }
            } else if (id == 4 && inside(mx, my, STATUS_PANEL_X + 19, 244, 145, 34)) {
                notice_until = 0;
                notice[0] = 0;
            } else if (id == 5) {
                if (inside(mx, my, STATUS_PANEL_X + 19, 168, 126, 34)) {
                    request_power_action(POWER_RESTART);
                } else if (inside(mx, my, STATUS_PANEL_X + 161, 168, 126, 34)) {
                    request_power_action(POWER_SHUTDOWN);
                }
            }
        }
        return;
    }
    if (my < 32) {
        if (mx >= TOP_STATUS_LEFT - 7 && mx < TOP_STATUS_LEFT - 7 + 4 * TOP_STATUS_STEP) {
            int id = (mx - (TOP_STATUS_LEFT - 7)) / TOP_STATUS_STEP + 2;
            top_panel = top_panel == id ? 0 : id;
            start_open = 0;
        }
        return;
    }
    if (start_open) {
        if (inside(mx, my, LAUNCHER_X, LAUNCHER_Y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT)) {
            if (inside(mx, my, LAUNCHER_X + LAUNCHER_WIDTH - 51, LAUNCHER_Y + 12, 40, 37)) {
                start_open = 0;
                return;
            }
            AppEntry apps[8];
            int count = apps_list(apps);
            for (int i = 0; i < count; i++)
                if (inside(mx, my, launcher_tile_x(i), launcher_tile_y(i), LAUNCHER_TILE_WIDTH, LAUNCHER_TILE_HEIGHT)) {
                    launch_app(apps[i].id);
                    break;
                }
        } else start_open = 0;
        return;
    }
    for (int k = WINDOW_COUNT - 1; k >= 0; k--) {
        int id = order[k];
        Window *w = &windows[id];
        if (!w->open || w->min || !inside(mx, my, w->x, w->y, w->w, w->h))
            continue;
        raise_window(id);
        int rx = mx - w->x, ry = my - w->y;
        if (id == 1) settings_search_active = 0;
        for (int i = 0; i < 3; i++) {
            if (!inside(rx, ry, window_control_x(w, i), WINDOW_CONTROL_TOP,
                        WINDOW_CONTROL_SIZE, WINDOW_CONTROL_SIZE))
                continue;
            if (i == 0) minimize_window(id);
            else if (i == 1) toggle_max(id);
            else close_window(id);
            return;
        }
        if (ry < (id == 1 ? WINDOW_TITLE_HEIGHT : 42)) {
            if (!w->max) {
                drag = id;
                dragx = rx;
                dragy = ry;
            }
            return;
        }
#ifdef MAYOS_TASK_MANAGER
        if (id == TASK_WINDOW) {
            may_task_click(rx - 1, ry - 43, w->w - 2, w->h - 44);
            return;
        }
#endif
        if (id == 4) {
            if (inside(rx, ry, 18, 51, 112, 30)) {
                explorer_navigate(rx < 55 ? -1 : rx < 94 ? 1 : 0);
            } else if (inside(rx, ry, 211, 94, w->w - 235, 28) && explorer_bookmark[0]) {
                copy(explorer_query, explorer_bookmark, sizeof(explorer_query));
                explorer_submit();
            } else if (inside(rx, ry, w->w - 90, 51, 72, 30)) {
                if (explorer_loading) {
                    platform_web_cancel();
                    explorer_loading = 0;
                    platform_web_poll(explorer_response, sizeof(explorer_response));
                } else explorer_submit();
            }
            return;
        }
        if (id == 1) {
            if (settings_tab == SETTINGS_ABOUT && inside(rx, ry, 282, 265, 30, 30)) {
                if (ticks - egg_last_click > 150)
                    egg_clicks = 0;
                egg_last_click = ticks;
                if (++egg_clicks >= 5)
                    open_easter(EGG_CAT);
                return;
            }
            if (rx < 241) {
                if (inside(rx, ry, 18, 82, 207, 34)) {
                    if (rx >= 201 && settings_search[0]) settings_search[0] = 0;
                    settings_search_active = 1;
                    return;
                }
                settings_search_active = 0;
                int row = 0;
                for (int tab = 0; tab < SETTINGS_TAB_COUNT; tab++) {
                    if (!settings_match(tab)) continue;
                    if (inside(rx, ry, 18, SETTINGS_ROW_Y + row * SETTINGS_ROW_HEIGHT, 207, 29)) {
                        select_settings(tab);
                        break;
                    }
                    row++;
                }
            } else if (settings_tab == SETTINGS_LANGUAGE) {
                int picked = language_hit(rx, ry, 264, 110, w->w - 286);
                if (picked >= 0) {
                    language_choice = (enum MayLocale)picked;
                    language_save_failed = 0;
                } else if (inside(rx, ry, 264, 326, 118, 30)) {
                    language_choice = display_language;
                    language_save_failed = 0;
                } else if (inside(rx, ry, w->w - 158, 326, 136, 30)) {
                    locale_apply();
                }
            } else if (settings_tab == SETTINGS_UPDATES) {
#ifdef MAYOS_LINUX
                ry += 12;
#endif
                if (!remote_loading && inside(rx, ry, 264, 202, 200, 34)) {
#ifdef MAYOS_LINUX
                    if (media_state != 1 && media_state != 3) {
                        if (media_state != 4) media_state = 0;
                        remote_update_start();
                    }
#else
                    remote_update_start();
#endif
                }
#ifdef MAYOS_LINUX
                if (remote_result == 1 && media_state <= 0 && inside(rx, ry, 264, 408, 230, 30)) {
                    int started = install_available ?
                        platform_install_start(remote_release.url, remote_release.sha256, remote_release.bytes, remote_release.tag) :
                        platform_media_start(remote_release.url, remote_release.sha256, remote_release.bytes);
                    media_state = started ? 1 : -1;
                    media_percent = 0;
                }
#endif
            } else if (settings_tab == SETTINGS_SOUND && sound_level >= 0) {
                int left = 264 + 56, span = w->w - 286 - 112;
                int value = sound_level;
                if (inside(rx, ry, left, 158, span + 1, 34)) value = (rx - left) * 100 / span;
                else if (inside(rx, ry, 279, 160, 32, 30)) value -= 10;
                else if (inside(rx, ry, w->w - 69, 160, 32, 30)) value += 10;
                if (value < 0) value = 0;
                if (value > 100) value = 100;
                if (value != sound_level) {
                    if (platform_audio_set_volume(value)) sound_level = platform_audio_volume();
                    else notify(pointer_label("无法设置该设备的音量", "Could not set output volume"));
                }
            } else if (settings_tab == SETTINGS_DISPLAY) {
                int previous = inside(rx, ry, 264, 373, 150, 30);
                int next = inside(rx, ry, 427, 373, 150, 30);
                if (previous || next) {
                    if (display_trial_deadline) display_trial_finish(next);
                    else if (platform_display_has_modes()) {
                        if (platform_display_cycle(next ? 1 : -1)) {
                            display_trial_deadline = (ticks + 1500u) | 1u;
                            display_trial_seconds = 15;
                        } else notify(pointer_label("设备未接受此显示模式", "The display rejected this mode"));
                    }
                }
                for (int mode = 0; mode < 4; mode++) {
                    if (!inside(rx, ry, 264 + mode * 132, 258, 122, 30)) continue;
                    display_set_scale_mode(mode);
                    serial("MayOS: display scaling changed\n");
                    preferences_save();
                    break;
                }
            } else if (settings_tab == SETTINGS_PERSONAL) {
                int cw = w->w - 286;
                if (inside(rx, ry, 264, 86, 142, 30)) desktop_options = 0;
                else if (inside(rx, ry, 418, 86, 142, 30)) desktop_options = 1;
                else if (desktop_options) {
                    for (int i = 0; i < DESKTOP_STYLE_COUNT; i++)
                        if (inside(rx, ry, 264 + i * 132, 161, 122, 30)) {
                            desktop_style = i;
                            preferences_save();
                        }
                    if (inside(rx, ry, 264, 269, 142, 30) ||
                        inside(rx, ry, 418, 269, 142, 30)) {
                        show_brand = rx < 418;
                        preferences_save();
                    } else if (inside(rx, ry, 264, 343, 174, 30)) {
                        preferences_reset();
                        notify(pointer_label("个性化设置已恢复默认", "Personalization restored"));
                    }
                } else {
                    if (inside(rx, ry, 360, 128, 128, 30) ||
                        inside(rx, ry, 494, 128, 128, 30) ||
                        inside(rx, ry, 628, 128, 148, 30)) {
                        cursor_style = rx < 494 ? POINTER_DARK : POINTER_LIGHT;
                        if (rx >= 628) pointer_size = 0;
                        preferences_save();
                    }
                    for (int i = 0; i < 3; i++)
                        if (inside(rx, ry, 360 + i * 101, 166, 94, 30)) {
                            pointer_size = i;
                            preferences_save();
                        }
                    for (int i = 0; i < POINTER_SOURCES; i++)
                        if (inside(rx, ry, 278 + i % 2 * 142, 249 + i / 2 * 36, 133, 30))
                            cursor_select(i, i == POINTER_PPM);
                    if (preview_cursor.count && inside(rx, ry, 264 + cw - 138, 379, 138, 30)) {
                        int remember = cursor_source_save(cursor_choice);
                        memcpy(&custom_cursor, &preview_cursor, sizeof(custom_cursor));
                        cursor_style = POINTER_CUSTOM;
                        cursor_active_source = cursor_choice;
                        cursor_started = ticks;
                        if (remember) preferences_save();
                        notify(pointer_label("自定义指针已应用", "Custom pointer applied"));
                        serial("MayOS: custom pointer applied: ");
                        serial(cursor_source_names[cursor_active_source]);
                        serial("\n");
                    }
                }
            } else if (settings_tab == SETTINGS_GENERAL) {
                if (inside(rx, ry, w->w - 185, 158, 146, 30)) begin_modal(4, -1);
                else if (disk_ready && inside(rx, ry, w->w - 185, 247, 146, 30))
                    notify(sync_workspace() ? "已同步到磁盘" : "磁盘不可写");
                else if (inside(rx, ry, 264, 337, 174, 30)) select_settings(SETTINGS_LANGUAGE);
                else if (inside(rx, ry, 450, 337, 186, 30)) select_settings(SETTINGS_ABOUT);
            } else if (settings_tab == SETTINGS_NETWORK && inside(rx, ry, 281, 284, 192, 30)) {
                launch_app(APP_EXPLORER);
            }
        } else if (id == 0) {
            if (rx < 146) {
                int nav = (ry - 103) / 40;
                if (ry >= 103 && nav < 4) {
                    folder = nav;
                    file_scroll = 0;
                    selected = -1;
                }
            } else if (ry >= 54 && ry < 84 && rx < 206) {
                files_up();
            } else if (ry >= 99 && ry < 129) {
                if (rx >= w->w - 120 && rx < w->w - 76) {
                    file_scroll -= 5;
                    if (file_scroll < 0)
                        file_scroll = 0;
                } else if (rx >= w->w - 66) {
                    build_file_list();
                    if (file_scroll + 5 < file_count)
                        file_scroll += 5;
                } else if (may_vfs_read_only(folder)) {
                    notify("只读系统目录");
                } else if (rx >= 164 && rx < 298)
                    begin_modal(1, -1);
                else if (rx >= 308 && rx < 456)
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
        } else if (id == 3) {
            if (editor_writable() && inside(rx, ry, 17, 55, 90, 30)) editor_key(0x106);
            else if (editor_writable() && inside(mx, my, editor_ime_left(w), w->y + 55, EDIT_IME_WIDTH, 30)) editor_key(MAYOS_KEY_IME);
            else if (editor_ime.size && inside(mx, my, w->x + 33, editor_ime_top(w) + 47,
                                               w->w - 66, 35)) {
                MayImeChoices choices;
                ime_convert(&editor_ime, display_language == MAY_ZH_HANT, &choices);
                int slot = choices.count == 1 ? 0 : (mx - w->x - 33) / editor_ime_cell(w);
                if (slot < IME_PAGE && ime_commit_to(&editor_ime, editor_buf, FILE_CAP,
                                                       editor_ime.selected / IME_PAGE * IME_PAGE + slot))
                    editor_check_dirty();
            } else if (inside(rx, ry, 17, 116, w->w - 34, w->h - 158)) {
                if (editor_ime.size &&
                    !ime_commit_to(&editor_ime, editor_buf, FILE_CAP, editor_ime.selected)) return;
                editor_select_at(w, mx, my);
                editor_anchor = editor_caret;
                editor_dragging = 1;
            }
        }
        return;
    }
    set_focus(-1);
    /* A real Notes.txt desktop icon with normal select/double-click behavior. */
    if (inside(mx, my, 120, 90, 100, 90)) {
        int note = fs_find(2, "Notes.txt");
        if (note >= 0) {
            if (desktop_note_selected && last_clicked == note &&
                ticks - last_click_tick < 45) {
                open_file(note);
                desktop_note_selected = 0;
                last_clicked = -1;
            } else {
                desktop_note_selected = 1;
                last_clicked = note;
                last_click_tick = ticks;
            }
        }
        return;
    }
    desktop_note_selected = 0;
}
static void right_click(void) {
#ifndef MAYOS_LINUX
    if (maygame_active()) return;
#endif
    if (egg_open)
        return;
    if (modal)
        return;
    int item = dock_hit(mx, my);
    dock_popup = -1;
    if ((item > DOCK_LAUNCHER && item < DOCK_TRASH) || item == DOCK_FIREFOX) {
        dock_popup = item;
        active_menu = menu_row = -1;
        context_open = start_open = top_panel = 0;
        dirty = 1;
        return;
    }
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
#ifndef MAYOS_LINUX
    if (maygame_active()) { maygame_key(k); return; }
#endif
    if (egg_open) {
        if (k == 27 || k == '\n')
            egg_open = EGG_CLOSED;
        return;
    }
    egg_clicks = 0;
    if (modal == MODAL_POWER) {
        if (k == 27 || k == '\n') modal = 0;
        return;
    }
    if (k == 0x105) {
        raise_window(2);
        return;
    }
#ifdef MAYOS_TASK_MANAGER
    if (k == MAYOS_KEY_TASK_MANAGER) {
        task_manager_open();
        return;
    }
#endif
    if (modal) {
        if (k == 27 && !modal_ime.size) modal = 0;
        else if (k == '\n' && !modal_ime.size) accept_modal();
        else if (modal < 5 && k != '\t') ime_key(&modal_ime, modal_text, 48, k);
        return;
    }
    if (k == MAYOS_KEY_LAUNCHER) {
        toggle_launcher();
        return;
    }
    if (active_menu >= 0) {
        if (k == 27) active_menu = menu_row = -1;
        else if (k == MAYOS_KEY_LEFT || k == MAYOS_KEY_RIGHT) {
            int direction = k == MAYOS_KEY_LEFT ? MENU_COUNT - 1 : 1;
            do {
                active_menu = (active_menu + direction) % MENU_COUNT;
            } while (!menu_label(active_menu));
            menu_row = -1;
        } else {
            MenuItem items[MENU_MAX_ITEMS];
            int count = menu_items(items);
            if (k == '\n' && menu_row >= 0 && menu_row < count)
                run_menu_item(items[menu_row]);
            else if (count && (k == 0x101 || k == 0x102)) {
                int direction = k == 0x101 ? count - 1 : 1;
                int row = menu_row < 0 || menu_row >= count ? (k == 0x101 ? 0 : count - 1) : menu_row;
                menu_row = -1;
                for (int i = 0; i < count; i++) {
                    row = (row + direction) % count;
                    if (!items[row].enabled) continue;
                    menu_row = row;
                    break;
                }
            }
        }
        return;
    }
    if (settings_search_active && !start_open && focus == 1 && windows[1].open && !windows[1].min) {
        u32 n = strlen(settings_search);
        if (k == 27) {
            settings_search_active = 0;
            settings_search[0] = 0;
        } else if (k == '\b' && n) settings_search[n - 1] = 0;
        else if (k == '\n') {
            for (int tab = 0; tab < SETTINGS_TAB_COUNT; tab++)
                if (settings_match(tab)) {
                    select_settings(tab);
                    settings_search_active = 0;
                    break;
                }
        } else if (k >= 32 && k < 127 && n + 1 < sizeof(settings_search) && text_width(settings_search) < 135) {
            settings_search[n] = k;
            settings_search[n + 1] = 0;
        }
        return;
    }
    if (!start_open && !top_panel && focus == 1 && windows[1].open &&
        !windows[1].min && settings_tab == SETTINGS_LANGUAGE) {
        if (k == '\n') {
            locale_apply();
            return;
        }
        if (k == 27 || k == 0x101 || k == 0x102 ||
            k == MAYOS_KEY_LEFT || k == MAYOS_KEY_RIGHT) {
            language_choice = k == 27 ? display_language : language_move(language_choice, k);
            language_save_failed = 0;
            return;
        }
    }
    if (k == 27 && focus == 3 && editor_ime.size && editor_writable()) {
        ime_clear(&editor_ime);
        return;
    }
    if (k == 27) {
#ifdef MAYOS_TASK_MANAGER
        if (focus == TASK_WINDOW && windows[TASK_WINDOW].open && !windows[TASK_WINDOW].min)
            may_task_key(k, windows[TASK_WINDOW].h - 44);
#endif
        start_open = context_open = top_panel = 0;
        dock_popup = -1;
        return;
    }
    if (start_open) {
        u32 n = strlen(search);
        AppEntry apps[8];
        int count = apps_list(apps);
        if (k == '\n') {
            if (launcher_row < count) launch_app(apps[launcher_row].id);
        } else if (count && (k == 0x101 || k == 0x102 || k == MAYOS_KEY_LEFT || k == MAYOS_KEY_RIGHT)) {
            int shift = k == 0x101 ? -LAUNCHER_COLUMNS : k == 0x102 ? LAUNCHER_COLUMNS :
                        k == MAYOS_KEY_LEFT ? -1 : 1;
            int next = launcher_row + shift;
            if (next >= 0 && next < count) launcher_row = next;
        } else if (k == '\b' && n) {
            search[n - 1] = 0;
            launcher_row = 0;
        } else if (k >= 32 && k < 127 && n < sizeof(search) - 1) {
            search[n] = k;
            search[n + 1] = 0;
            launcher_row = 0;
        }
        return;
    }
#ifdef MAYOS_TASK_MANAGER
    if (focus == TASK_WINDOW && windows[TASK_WINDOW].open && !windows[TASK_WINDOW].min) {
        may_task_key(k, windows[TASK_WINDOW].h - 44);
        return;
    }
#endif
    if (focus == 4 && windows[4].open && !windows[4].min) {
        if (k == 0x101 || k == 0x102) {
            if (k == 0x101 && explorer_scroll > 0) explorer_scroll--;
            if (k == 0x102) {
                const Window *window = &windows[4];
                const char *s = explorer_response, *end;
                int rows = 0;
                while (*s) {
                    s = explorer_line(s, window->w - 74, &end);
                    rows++;
                }
                if (explorer_scroll < rows - explorer_visible_rows(window)) explorer_scroll++;
            }
            return;
        }
        int n = strlen(explorer_query);
        if (k == '\n') explorer_submit();
        else if (k == '\b' && n) explorer_query[n - 1] = 0;
        else if (k >= 32 && k < 127 && n < (int)sizeof(explorer_query) - 1) {
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
    } else if (focus == 3 && windows[3].open && !windows[3].min) {
        editor_key(k);
    } else if (focus == 0 && windows[0].open && !windows[0].min) {
        if (k == '\n')
            open_file(selected);
        else if (k == 0x103)
            trash_selected();
        else if (k == '\b') {
            files_up();
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
#ifndef MAYOS_LINUX
static void setup_card(int x, int y, u32 base, u32 edge, int chosen,
                       const char *title, const char *subtitle) {
    soft_roundrect(x, y, 244, 128, 16, chosen ? 0xceced2 : 0x303034);
    soft_roundrect(x + 1, y + 1, 242, 126, 15, 0x151518);
    soft_roundrect(x + 14, y + 14, 62, 42, 8, edge);
    soft_roundrect(x + 15, y + 15, 60, 40, 7, base);
    rect(x + 23, y + 24, 43, 2, edge);
    rect(x + 23, y + 44, 25, 2, edge);
    text(x + 15, y + 69, title, 0xf5f5f6);
    text(x + 15, y + 95, subtitle, 0xa5a5ab);
    if (chosen) {
        soft_roundrect(x + 201, y + 16, 22, 22, 11, 0xf2f2f4);
        stroke(x + 207, y + 27, x + 211, y + 31, 2, 0x202024);
        stroke(x + 211, y + 31, x + 218, y + 22, 2, 0x202024);
    }
}

enum SetupStep { SETUP_WELCOME, SETUP_LANGUAGE, SETUP_LICENSE, SETUP_PROFILE,
                 SETUP_UPDATES, SETUP_APPEARANCE, SETUP_REVIEW, SETUP_COUNT };
static const char *const setup_prompts[SETUP_COUNT] = {
    "MayOS: first boot setup ready\n", "MayOS: first boot language ready\n",
    "MayOS: first boot license ready\n", "MayOS: first boot profile ready\n",
    "MayOS: first boot updates ready\n", "MayOS: first boot appearance ready\n",
    "MayOS: first boot review ready\n"
};

static void setup_license(void) {
    static const char *const lines[] = {
        "MIT License",
        "Copyright (c) 2024 MayOS Project",
        "Permission is hereby granted, free of charge, to any person",
        "obtaining a copy of this software and associated documentation",
        "files (the \"Software\"), to deal in the Software without",
        "restriction, including without limitation the rights to use, copy,",
        "modify, merge, publish, distribute, sublicense, and/or sell copies",
        "of the Software, and to permit persons to whom the Software is",
        "furnished to do so, subject to the following conditions:",
        "The above copyright notice and this permission notice shall be",
        "included in all copies or substantial portions of the Software.",
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,",
        "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF",
        "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND",
        "NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT",
        "HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,",
        "WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,",
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER",
        "DEALINGS IN THE SOFTWARE."
    };
    soft_roundrect(482, 212, 573, 288, 15, 0x17171b);
    int y = 220;
    for (u32 i = 0; i < sizeof(lines) / sizeof(lines[0]); i++, y += 14) {
        if (i == 2 || i == 9 || i == 11) y += 2;
        text(500, y, lines[i], i < 2 ? MAY_DARK_INK : 0xc3c3cc);
    }
}

static void setup_title(const char *title, u32 color) {
    if (text_width(title) * 2 <= 573) large_text(482, 150, title, color);
    else heading_text(482, 153, title, color);
}

static void setup_screen(enum SetupStep step, const char *name, int theme, int error) {
    desktop_wallpaper();
    soft_roundrect(175, 91, 930, 538, 31, MAY_DARK_BACKGROUND);
    soft_roundrect(181, 87, 918, 530, 27, MAY_DARK_LINE);
    soft_roundrect(182, 88, 916, 528, 26, MAY_DARK_PANEL);
    rect(438, 108, 1, 483, MAY_DARK_LINE);
    reference_monitor_icon(214, 124, MAY_DARK_INK, MAY_DARK_PANEL);
    reference_text(252, 112, "MayOS", MAY_DARK_INK, 1);
    text(214, 172, L(FIRST_BOOT), MAY_DARK_MUTED);
    const char *steps[SETUP_COUNT] = {L(WELCOME), L(LANGUAGE), L(LICENSE), L(PROFILE),
                                    L(UPDATES), L(APPEARANCE), L(READY)};
    for (enum SetupStep i = SETUP_WELCOME; i < SETUP_COUNT; i++) {
        int y = 215 + i * 52;
        if (step == i) soft_roundrect(207, y, 204, 42, 12, MAY_DARK_SELECTION);
        soft_roundrect(217, y + 12, 18, 18, 9, i <= step ? 0xd5d5db : 0x45454d);
        if (i > step) soft_roundrect(218, y + 13, 16, 16, 8, MAY_DARK_PANEL);
        text(250, y + 9, steps[i], i <= step ? MAY_DARK_INK : 0xa5a5ab);
    }
    rect(482, 507, 573, 1, MAY_DARK_LINE);
    if (step == SETUP_WELCOME) {
        setup_title(L(WELCOME_TITLE), MAY_DARK_INK);
        text(482, 224, L(WELCOME_HINT), MAY_DARK_MUTED);
        soft_roundrect(482, 285, 506, 110, 18, MAY_DARK_SURFACE);
        reference_monitor_icon(505, 321, 0xe7e7eb, MAY_DARK_SURFACE);
        text(552, 310, L(KERNEL_DESKTOP), MAY_DARK_INK);
        text(552, 345, L(FILES_HINT), MAY_DARK_MUTED);
    } else if (step == SETUP_LANGUAGE) {
        setup_title(L(CHOOSE), MAY_DARK_INK);
        text(482, 224, L(CHOOSE_HINT), MAY_DARK_MUTED);
        language_grid(482, 266, 573, language_choice, MAY_DARK_INK, MAY_DARK_MUTED);
    } else if (step == SETUP_LICENSE) {
        setup_title(L(LICENSE_TITLE), MAY_DARK_INK);
        setup_license();
    } else if (step == SETUP_PROFILE) {
        setup_title(L(PROFILE_TITLE), MAY_DARK_INK);
        text(482, 224, L(PROFILE_HINT), MAY_DARK_MUTED);
        text(482, 273, L(USERNAME), MAY_DARK_INK);
        soft_roundrect(482, 303, 506, 53, 12, 0x85858c);
        soft_roundrect(483, 304, 504, 51, 11, MAY_DARK_SURFACE);
        int end = text(500, 319, name[0] ? name : "may", name[0] ? MAY_DARK_INK : 0xa5a5ab);
        rect(name[0] ? end + 2 : 499, 319, 1, 18, MAY_DARK_INK);
        text(482, 373, L(USER_RULE), MAY_DARK_MUTED);
        text(482, 403, L(USER_DEFAULT), MAY_DARK_MUTED);
    } else if (step == SETUP_UPDATES) {
        setup_title(L(UPDATES), MAY_DARK_INK);
        int end = text(482, 224, L(CURRENT_VERSION), MAY_DARK_MUTED);
        text(end + 24, 224, MAYOS_RELEASE, MAY_DARK_MUTED);
        soft_roundrect(482, 279, 506, 135, 16, MAY_DARK_SURFACE);
        text(505, 302, L(MANUAL_UPDATE), MAY_DARK_INK);
        text(505, 337, L(UPDATE_IMAGE), MAY_DARK_MUTED);
        text(505, 368, L(UPDATE_CONFIRM), MAY_DARK_MUTED);
    } else if (step == SETUP_APPEARANCE) {
        setup_title(L(APPEARANCE_TITLE), MAY_DARK_INK);
        text(482, 224, L(APPEARANCE_HINT), MAY_DARK_MUTED);
        setup_card(482, 279, 0x111113, 0x787881, theme == 0, L(DARK), L(DARK_HINT));
        setup_card(744, 279, 0xeaeaec, 0xa3a3aa, theme == 1, L(LIGHT), L(LIGHT_HINT));
    } else {
        setup_title(L(READY_TITLE), MAY_DARK_INK);
        text(482, 224, L(READY_HINT), MAY_DARK_MUTED);
        soft_roundrect(482, 271, 506, 158, 16, MAY_DARK_SURFACE);
        text(505, 292, L(DISPLAY_LANGUAGE), MAY_DARK_MUTED);
        text(690, 292, languages[display_language].name, MAY_DARK_INK);
        rect(505, 329, 460, 1, 0x34343a);
        text(505, 341, L(USERNAME), MAY_DARK_MUTED);
        text(690, 341, name[0] ? name : "may", MAY_DARK_INK);
        rect(505, 378, 460, 1, 0x34343a);
        text(505, 390, L(APPEARANCE), MAY_DARK_MUTED);
        text(690, 390, theme == 0 ? L(DARK) : L(LIGHT), MAY_DARK_INK);
    }
    if (error) text(482, 475, L(SAVE_ERROR), 0xffaf9f);
    if (step != SETUP_WELCOME) {
        soft_roundrect(483, 529, 108, 46, 12, MAY_DARK_SURFACE);
        center_text(483, 108, 541, L(BACK), 0xe7e7eb);
    }
    soft_roundrect(889, 529, 166, 46, 12, MAY_DARK_ACTION);
    const char *next = step == SETUP_REVIEW ? L(FINISH) :
                       step == SETUP_LICENSE ? L(AGREE) :
                       step == SETUP_WELCOME ? L(START) : L(CONTINUE);
    center_text(889, 166, 541, next, 0x151519);
    if (step == SETUP_LICENSE) {
        soft_roundrect(647, 529, 152, 46, 12, MAY_DARK_SURFACE);
        center_text(647, 152, 541, L(DECLINE), 0xe7e7eb);
    } else {
        center_text(647, 152, 545, "Enter", 0xa5a5ab);
    }
    cursor();
    present(pixels);
}

static int setup_save(const char *name, int theme) {
    int index = fs_find(0, profile_file);
    int existing = index >= 0 && profile_valid(&files[index]);
    if (name[0] && strcmp(name, "may")) {
        if (!existing && index == -1) index = fs_create(0, profile_file, 0);
        else if (!existing) return 0;
        if (index < 0) return 0;
        u32 start = sizeof(profile_header) - 1;
        memcpy(files[index].data, profile_header, start);
        memcpy(files[index].data + start, name, strlen(name));
        files[index].size = start + strlen(name);
    } else if (existing) {
        fs_remove(index);
    }
    int previous_theme = dark;
    dark = theme == 0;
    if (preferences_save()) {
        copy(profile_name, name[0] ? name : "may", sizeof(profile_name));
        return 1;
    }
    dark = previous_theme;
    return 0;
}

static void installed_first_boot(void) {
    enum SetupStep step = SETUP_WELCOME;
    enum MayLocale initial_language = display_language;
    char name[sizeof(profile_name)] = "";
    if (strcmp(profile_name, "may")) copy(name, profile_name, sizeof(name));
    int theme = 0, error = 0, previous = 0;
    setup_screen(step, name, theme, error);
    serial(setup_prompts[step]);
    for (;;) {
        int key = 0;
        if (input_poll(&mx, &my, &buttons, &key)) {
            enum SetupStep old_step = step;
            int click = (buttons & 1) && !(previous & 1);
            previous = buttons;
            if (step == SETUP_LANGUAGE) {
                int picked = click ? language_hit(mx, my, 482, 266, 573) : -1;
                if (picked >= 0) language_choice = (enum MayLocale)picked;
                else language_choice = language_move(language_choice, key);
            }
            if (step == SETUP_PROFILE) {
                u32 length = strlen(name);
                if (key == '\b' && length) name[length - 1] = 0;
                if (profile_character(key) && length + 1 < sizeof(name)) {
                    name[length] = (char)key;
                    name[length + 1] = 0;
                }
            }
            if (step == SETUP_APPEARANCE) {
                if (key == 0x101 || key == '1' || (click && inside(mx, my, 482, 279, 244, 128)))
                    theme = 0;
                if (key == 0x102 || key == '2' || (click && inside(mx, my, 744, 279, 244, 128)))
                    theme = 1;
            }
            if (step == SETUP_LICENSE &&
                (key == 'n' || key == 'N' || (click && inside(mx, my, 647, 529, 152, 46)))) {
                serial("MayOS: first boot license declined\n");
                poweroff();
                step = SETUP_WELCOME;
            } else if (step != SETUP_WELCOME &&
                       (key == 27 || (key == '\b' && step != SETUP_PROFILE) ||
                        (click && inside(mx, my, 483, 529, 108, 46)))) {
                step--;
                language_choice = display_language;
            } else if (key == '\n' || (step == SETUP_LICENSE && (key == 'y' || key == 'Y')) ||
                       (click && inside(mx, my, 889, 529, 166, 46))) {
                if (step != SETUP_REVIEW) {
                    if (step == SETUP_LANGUAGE) {
                        display_language = language_choice;
                        serial("MayOS: first boot display language selected: ");
                        serial(languages[display_language].tag);
                        serial("\n");
                    }
                    step++;
                } else {
                    LocaleEdit edit = {0};
                    int changed = display_language != initial_language;
                    if (!changed || locale_stage(display_language, &edit)) {
                        if (setup_save(name, theme)) {
                            oldbuttons = buttons;
                            serial("MayOS: first boot setup completed\n");
                            return;
                        }
                        if (changed) locale_unstage(&edit);
                    }
                    error = 1;
                }
            }
            if (step != old_step) error = 0;
            setup_screen(step, name, theme, error);
            if (step != old_step) serial(setup_prompts[step]);
        }
        platform_idle();
    }
}
#endif

void kernel_main(u32 magic, u32 info) {
    hardware_init(magic, info);
    fs_init();
    may_vfs_init();
    preferences_load();
    locale_load();
    language_choice = display_language;
    boot_splash_step("Applying desktop settings");
#ifndef MAYOS_LINUX
    profile_load();
    if (fs_installed_setup_pending()) {
        boot_splash_finish("Opening first-boot setup");
        installed_first_boot();
    } else
#endif
    boot_splash_finish("Opening MayOS desktop");
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
                editor_dragging = 0;
            } else if (editor_dragging && focus == 3) {
                editor_select_at(&windows[3], mx, my);
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
                if (w->y > DOCK_TOP - w->h - 8)
                    w->y = DOCK_TOP - w->h - 8;
            }
            oldbuttons = buttons;
            if (key)
                key_event(key);
        }
        if (display_trial_deadline) {
            i32 remaining = (i32)(display_trial_deadline - ticks);
            if (remaining <= 0) display_trial_finish(0);
            else if (display_trial_seconds != (unsigned)(remaining + 99) / 100) {
                display_trial_seconds = (unsigned)(remaining + 99) / 100;
                dirty = 1;
            }
        }
        remote_update_poll();
#ifdef MAYOS_LINUX
        if (media_state == 1 || media_state == 3) {
            int before = media_percent, previous_state = media_state;
            media_state = platform_media_poll(&media_percent);
            if (before != media_percent || media_state != previous_state) dirty = 1;
            if (media_state == 4) serial("MayOS: online update staged for trial boot\n");
        }
#endif
        if (explorer_loading) {
            explorer_loading = platform_web_poll(explorer_response, sizeof(explorer_response));
            dirty = 1;
        }
        if (ticks / 50 != last) {
            last = ticks / 50;
#ifdef MAYOS_LINUX
            platform_wallpaper_poll(daily_wallpaper, wallpaper_date,
                                    desktop_style == DESKTOP_BING && !high_contrast);
#endif
            dirty = 1;
        }
#ifdef MAYOS_TASK_MANAGER
        if (windows[TASK_WINDOW].open && !windows[TASK_WINDOW].min) {
            u32 visible = 0;
            for (int i = 0; i < WINDOW_COUNT; i++)
                visible += windows[i].open && !windows[i].min;
            if (may_task_poll(ticks, visible)) dirty = 1;
        }
#endif
#ifndef MAYOS_LINUX
        may_net_poll(); /* bounded polling, no IRQ or user-space dependency */
        may_time_poll();
#ifdef MAYOS_ROUGHTIME
        may_roughtime_poll();
#endif
        if (platform_wallpaper_poll(daily_wallpaper, wallpaper_date,
                                    desktop_style == DESKTOP_BING && !high_contrast))
            dirty = 1;
#ifdef MAYOS_TLS_WIRE_DIAGNOSTIC
        may_tls_wire_poll();
#endif
#endif
        static u32 last_pointer_frame, last_preview_frame;
        u32 pointer_frame = cursor_style == POINTER_CUSTOM ? may_cursor_frame(&custom_cursor, ticks - cursor_started) : 0;
        u32 preview_frame = windows[1].open && !windows[1].min && settings_tab == SETTINGS_PERSONAL && !desktop_options ?
            may_cursor_frame(&preview_cursor, ticks - cursor_preview_start) : 0;
        if (pointer_frame != last_pointer_frame || preview_frame != last_preview_frame) dirty = 1;
        last_pointer_frame = pointer_frame;
        last_preview_frame = preview_frame;
        if (animation_step(mx, my)) dirty = 1;
        if (dirty)
            render();
        platform_idle();
    }
}

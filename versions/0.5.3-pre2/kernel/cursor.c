#include "cursor.h"
#include "cursor_inflate.h"

#define TAG(a,b,c,d) ((u32)(a) | (u32)(b) << 8 | (u32)(c) << 16 | (u32)(d) << 24)
enum { MAX_SOURCE_EDGE = 256 };
static u8 png_compressed[MAY_CURSOR_FILE_MAX];
static u8 png_scanlines[MAX_SOURCE_EDGE * (1 + MAX_SOURCE_EDGE * 4)];

static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }
static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void layout(MayCursorFrame *frame, u32 w, u32 h, u32 hx, u32 hy) {
    u32 edge = w > h ? w : h;
    if (edge < MAY_CURSOR_EDGE) edge = MAY_CURSOR_EDGE;
    frame->width = (u16)((w * MAY_CURSOR_EDGE + edge - 1) / edge);
    frame->height = (u16)((h * MAY_CURSOR_EDGE + edge - 1) / edge);
    frame->hotspot_x = (u16)(hx * frame->width / w);
    frame->hotspot_y = (u16)(hy * frame->height / h);
}

static u32 image_sample(const u8 *row, u32 x, u32 bits) {
    u32 at = x * bits;
    return (row[at / 8] >> (8 - bits - at % 8)) & ((1u << bits) - 1);
}

static u32 mask_color(u32 color, u32 mask) {
    if (!mask) return 0;
    while (!(mask & 1)) { color >>= 1; mask >>= 1; }
    return (u32)(((u64)(color & mask) * 255 + mask / 2) / mask);
}

static int dib_frame(const u8 *data, u32 size, MayCursorFrame *frame, u32 w, u32 h) {
    if (size < 40) return 0;
    u32 header = le32(data), compression = le32(data + 16), bits = le16(data + 14);
    u32 raw_height = le32(data + 8), at = header;
    if (header < 40 || header > size || le32(data + 4) != w || le16(data + 12) != 1 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) return 0;
    int top_down = (raw_height >> 31) != 0;
    u32 dib_height = top_down ? 0u - raw_height : raw_height;
    if (dib_height != h * 2 && (dib_height != h || bits != 32)) return 0;
    if (compression != 0 && (compression != 3 || (bits != 16 && bits != 32))) return 0;
    u32 red = bits == 16 ? 0x7c00u : 0xff0000u;
    u32 green = bits == 16 ? 0x03e0u : 0x00ff00u;
    u32 blue = bits == 16 ? 0x001fu : 0x0000ffu;
    u32 alpha = bits == 32 ? 0xff000000u : 0;
    if (compression == 3) {
        if (size < 52 || (header != 40 && header < 52)) return 0;
        red = le32(data + 40); green = le32(data + 44); blue = le32(data + 48);
        alpha = header >= 56 ? le32(data + 52) : 0;
        if (!red || !green || !blue || (red & green) || (red & blue) || (green & blue) ||
            (alpha & (red | green | blue))) return 0;
        if (header == 40) at += 12;
    }
    u32 colors = 0;
    const u8 *palette = data + at;
    if (bits <= 8) {
        colors = le32(data + 32);
        if (!colors) colors = 1u << bits;
        if (colors > (1u << bits) || colors * 4 > size - at) return 0;
        at += colors * 4;
    }
    u32 stride = ((w * bits + 31) / 32) * 4;
    u32 mask_stride = ((w + 31) / 32) * 4;
    if (stride * h > size - at) return 0;
    const u8 *xor = data + at, *mask = xor + stride * h;
    int has_mask = dib_height == h * 2 && mask_stride * h <= size - at - stride * h;
    int has_alpha = 0;
    if (alpha && bits == 32) {
        for (u32 y = 0; y < h && !has_alpha; y++)
            for (u32 x = 0; x < w; x++)
                if (le32(xor + y * stride + x * 4) & alpha) { has_alpha = 1; break; }
    }
    if (!has_mask && !has_alpha) return 0;

    for (u32 y = 0; y < frame->height; y++) {
        u32 src_y = y * h / frame->height;
        if (!top_down) src_y = h - src_y - 1;
        const u8 *row = xor + src_y * stride;
        for (u32 x = 0; x < frame->width; x++) {
            u32 src_x = x * w / frame->width, rgb, a = 255;
            if (bits <= 8) {
                u32 index = image_sample(row, src_x, bits);
                if (index >= colors) return 0;
                rgb = le32(palette + index * 4) & 0xffffff;
            } else if (bits == 24) {
                const u8 *p = row + src_x * 3;
                rgb = (u32)p[2] << 16 | (u32)p[1] << 8 | p[0];
            } else {
                u32 pixel = bits == 16 ? le16(row + src_x * 2) : le32(row + src_x * 4);
                rgb = mask_color(pixel, red) << 16 | mask_color(pixel, green) << 8 | mask_color(pixel, blue);
                if (has_alpha) a = mask_color(pixel, alpha);
            }
            if (has_alpha) {
                if (!a) rgb = 0;
            } else if (has_mask && image_sample(mask + src_y * mask_stride, src_x, 1)) {
                a = 0;
                /* Old Windows cursors use the RGB bits as XOR when AND is set. */
            }
            frame->pixels[y * frame->width + x] = a << 24 | rgb;
        }
    }
    return 1;
}

static u32 crc(const u8 *bytes, u32 size) {
    u32 value = ~0u;
    for (u32 i = 0; i < size; i++) {
        value ^= bytes[i];
        for (int bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
    }
    return ~value;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, da = p - a, db = p - b, dc = p - c;
    if (da < 0) da = -da;
    if (db < 0) db = -db;
    if (dc < 0) dc = -dc;
    if (da <= db && da <= dc) return a;
    return db <= dc ? b : c;
}

static int png_frame(const u8 *data, u32 size, MayCursorFrame *frame, u32 w, u32 h) {
    if (size < 33 || be32(data + 8) != 13 || le32(data + 12) != TAG('I','H','D','R') ||
        be32(data + 16) != w || be32(data + 20) != h || data[26] || data[27] || data[28])
        return 0; /* Interlaced or 16-bit PNG is outside the pointer import subset. */
    u32 depth = data[24], type = data[25], channels;
    if (type == 0 || type == 3) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return 0;
        channels = 1;
    } else {
        if (depth != 8 || (type != 2 && type != 4 && type != 6)) return 0;
        channels = type == 2 ? 3 : type == 4 ? 2 : 4;
    }
    const u8 *palette = 0, *alpha_table = 0;
    u32 colors = 0, alpha_count = 0, packed = 0, done = 0;
    for (u32 at = 8; at <= size - 12;) {
        u32 bytes = be32(data + at), tag = le32(data + at + 4);
        if (bytes > size - at - 12 || crc(data + at + 4, bytes + 4) != be32(data + at + 8 + bytes))
            return 0;
        const u8 *p = data + at + 8;
        if (tag == TAG('P','L','T','E')) {
            if (!bytes || bytes % 3 || bytes > 768) return 0;
            palette = p;
            colors = bytes / 3;
        } else if (tag == TAG('t','R','N','S')) {
            if ((type == 0 && bytes != 2) || (type == 2 && bytes != 6) ||
                (type == 3 && (!palette || bytes > colors)) || type == 4 || type == 6) return 0;
            alpha_table = p;
            alpha_count = bytes;
        } else if (tag == TAG('I','D','A','T')) {
            if (bytes > sizeof(png_compressed) - packed) return 0;
            memcpy(png_compressed + packed, p, bytes);
            packed += bytes;
        } else if (tag == TAG('I','E','N','D')) {
            if (bytes) return 0;
            done = 1;
            break;
        } else if (tag != TAG('I','H','D','R') && !(tag & 32u)) return 0;
        at += bytes + 12;
    }
    u32 row_size = (w * depth * channels + 7) / 8 + 1;
    u32 pixel_bytes = (depth * channels + 7) / 8;
    if (!done || !packed || (type == 3 && !palette) ||
        !cursor_inflate(png_compressed, packed, png_scanlines, row_size * h)) return 0;
    for (u32 y = 0; y < h; y++) {
        u8 *row = png_scanlines + y * row_size;
        u32 filter = row[0];
        if (filter > 4) return 0;
        for (u32 x = 1; x < row_size; x++) {
            int a = x > pixel_bytes ? row[x - pixel_bytes] : 0;
            int b = y ? row[(int)x - (int)row_size] : 0;
            int c = y && x > pixel_bytes ? row[(int)x - (int)row_size - (int)pixel_bytes] : 0;
            row[x] += (u8)(filter == 0 ? 0 : filter == 1 ? a : filter == 2 ? b :
                                      filter == 3 ? (a + b) / 2 : paeth(a, b, c));
        }
    }
    for (u32 y = 0; y < frame->height; y++) {
        const u8 *row = png_scanlines + (y * h / frame->height) * row_size + 1;
        for (u32 x = 0; x < frame->width; x++) {
            u32 src_x = x * w / frame->width, r, g, b, a = 255;
            const u8 *p = row + src_x * channels;
            if (type == 3) {
                u32 index = image_sample(row, src_x, depth);
                if (index >= colors) return 0;
                r = palette[index * 3]; g = palette[index * 3 + 1]; b = palette[index * 3 + 2];
                if (index < alpha_count) a = alpha_table[index];
            } else if (type == 0 || type == 4) {
                u32 sample = image_sample(row, type == 4 ? src_x * 2 : src_x, depth);
                r = g = b = sample * 255 / ((1u << depth) - 1);
                if (type == 4) a = p[1];
                else if (alpha_table && sample == ((u32)alpha_table[0] << 8 | alpha_table[1])) a = 0;
            } else {
                r = p[0]; g = p[1]; b = p[2];
                if (type == 6) a = p[3];
                else if (alpha_table && !(alpha_table[0] | alpha_table[2] | alpha_table[4]) &&
                         r == alpha_table[1] && g == alpha_table[3] && b == alpha_table[5]) a = 0;
            }
            frame->pixels[y * frame->width + x] = a ? a << 24 | r << 16 | g << 8 | b : 0;
        }
    }
    return 1;
}

static int cursor_image(const u8 *data, u32 size, MayCursorFrame *frame, int allow_icon) {
    if (size < 22 || le16(data) != 0) return 0;
    u32 type = le16(data + 2), count = le16(data + 4);
    if ((type != 2 && !(allow_icon && type == 1)) || !count || count > 64 ||
        count > (size - 6) / 16) return 0;
    u32 upper = ~0u;
    /* Pick the nearest valid representation to our 64px atlas. */
    for (u32 attempts = 0; attempts < count; attempts++) {
        u32 best = ~0u;
        const u8 *chosen = 0;
        for (u32 i = 0; i < count; i++) {
            const u8 *entry = data + 6 + i * 16;
            u32 w = entry[0] ? entry[0] : 256, h = entry[1] ? entry[1] : 256;
            u32 edge = w > h ? w : h;
            u32 delta = edge > MAY_CURSOR_EDGE ? edge - MAY_CURSOR_EDGE : MAY_CURSOR_EDGE - edge;
            u32 score = delta * 64 + i;
            if (upper != ~0u && score <= upper) continue;
            if (score < best) { chosen = entry; best = score; }
        }
        if (!chosen) break;
        upper = best;
        u32 w = chosen[0] ? chosen[0] : 256, h = chosen[1] ? chosen[1] : 256;
        u32 offset = le32(chosen + 12), length = le32(chosen + 8);
        u32 hx = type == 2 ? le16(chosen + 4) : 0, hy = type == 2 ? le16(chosen + 6) : 0;
        if (hx >= w || hy >= h || offset < 6 + count * 16 || offset > size || length > size - offset)
            continue;
        layout(frame, w, h, hx, hy);
        const u8 *p = data + offset;
        int png = length >= 8 && le32(p) == 0x474e5089 && le32(p + 4) == 0x0a1a0a0d;
        if (png ? png_frame(p, length, frame, w, h) : dib_frame(p, length, frame, w, h)) return 1;
    }
    return 0;
}

static int ani_frames(const u8 *p, u32 length, MayCursor *cursor) {
    for (u32 at = 0; at < length;) {
        if (length - at < 8) return 0;
        u32 size = le32(p + at + 4);
        if (size > length - at - 8 || (size & 1) > length - at - 8 - size) return 0;
        if (le32(p + at) == TAG('i','c','o','n')) {
            if (cursor->count == MAY_CURSOR_FRAMES ||
                !cursor_image(p + at + 8, size, &cursor->frames[cursor->count], 1)) return 0;
            cursor->count++;
        }
        at += 8 + size + (size & 1);
    }
    return 1;
}

static int cursor_animation(const u8 *data, u32 length, MayCursor *cursor) {
    u32 riff = le32(data + 4);
    if (riff < 4 || riff > length - 8 || le32(data + 8) != TAG('A','C','O','N')) return 0;
    const u8 *header = 0, *rates = 0, *sequence = 0;
    u32 rate_count = 0, sequence_count = 0;
    for (u32 at = 12, end = riff + 8; at < end;) {
        if (end - at < 8) return 0;
        const u8 *p = data + at + 8;
        u32 size = le32(data + at + 4), tag = le32(data + at);
        if (size > end - at - 8 || (size & 1) > end - at - 8 - size) return 0;
        if (tag == TAG('a','n','i','h') && !header && size >= 36 && le32(p) >= 36) header = p;
        else if (tag == TAG('r','a','t','e') && size % 4 == 0) { rates = p; rate_count = size / 4; }
        else if (tag == TAG('s','e','q',' ') && size % 4 == 0) { sequence = p; sequence_count = size / 4; }
        else if (tag == TAG('L','I','S','T') && size >= 4 && le32(p) == TAG('f','r','a','m') &&
                 !ani_frames(p + 4, size - 4, cursor)) return 0;
        at += 8 + size + (size & 1);
    }
    if (!header || !cursor->count || le32(header + 4) != cursor->count || !(le32(header + 32) & 1))
        return 0;
    u32 steps = le32(header + 8), fallback = le32(header + 28);
    int indexed = (le32(header + 32) & 2) != 0;
    if (!steps) steps = cursor->count;
    if (steps > MAY_CURSOR_STEPS || (indexed && (!sequence || sequence_count < steps))) return 0;
    for (u32 i = 0; i < steps; i++) {
        u32 index = indexed ? le32(sequence + 4 * i) : i % cursor->count;
        u32 rate = i < rate_count ? le32(rates + 4 * i) : fallback;
        if (index >= cursor->count || rate > 3600) return 0;
        cursor->sequence[i] = (u8)index;
        cursor->step_time[i] = (u16)(rate ? rate : 6);
        cursor->duration += cursor->step_time[i];
    }
    cursor->steps = steps;
    return 1;
}

int may_cursor_decode(const u8 *data, u32 size, MayCursor *out) {
    out->steps = out->count = out->duration = 0;
    if (size < 12 || size > MAY_CURSOR_FILE_MAX || !data) return 0;
    if (le32(data) == TAG('R','I','F','F')) {
        if (cursor_animation(data, size, out)) return 1;
    } else if (cursor_image(data, size, &out->frames[0], 0)) {
        out->count = out->steps = out->duration = 1;
        out->sequence[0] = 0;
        out->step_time[0] = 1;
        return 1;
    }
    out->count = out->steps = out->duration = 0;
    return 0;
}

u32 may_cursor_frame(const MayCursor *cursor, u32 hundredths) {
    if (!cursor->duration || cursor->steps <= 1) return cursor->steps ? cursor->sequence[0] : 0;
    u32 position = (u32)(((u64)hundredths * 60 / 100) % cursor->duration);
    for (u32 i = 0; i < cursor->steps; i++) {
        if (position < cursor->step_time[i]) return cursor->sequence[i];
        position -= cursor->step_time[i];
    }
    return 0;
}

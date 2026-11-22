#ifndef MAYOS_FRAMEBUFFER_H
#define MAYOS_FRAMEBUFFER_H
#include "core.h"
/* Standard firmware framebuffer adapter, shared by both kernels.
 * The desktop is authored at native 1280x720; other firmware modes scale. */
typedef struct {
    u8 *data;
    u32 width, height, pitch, bpp, xoffset, yoffset;
    u32 red, red_bits, green, green_bits, blue, blue_bits, alpha, alpha_bits;
} MaySurface;
static int surface_valid(const MaySurface *s, u64 capacity) {
    if (s->width < 640 || s->height < 480 || s->width > 16384 || s->height > 16384 ||
        (s->bpp != 16 && s->bpp != 24 && s->bpp != 32)) return 0;
    u32 bits[] = {s->red_bits, s->green_bits, s->blue_bits, s->alpha_bits};
    u32 shifts[] = {s->red, s->green, s->blue, s->alpha};
    u32 used = 0;
    for (int i = 0; i < 4; i++) {
        if (i == 3 && !bits[i]) continue;
        if (!bits[i] || bits[i] > 8 || shifts[i] >= s->bpp || bits[i] > s->bpp - shifts[i]) return 0;
        u32 mask = ((1U << bits[i]) - 1) << shifts[i];
        if (used & mask) return 0;
        used |= mask;
    }
    u64 row = ((u64)s->xoffset + s->width) * (s->bpp / 8);
    u64 span = ((u64)s->yoffset + s->height - 1) * s->pitch + row;
    return row <= s->pitch && span <= capacity &&
           (sizeof(void *) == 8 || span <= 0xffffffffULL);
}
static u32 surface_channel(u32 value, u32 bits, u32 shift) {
    return ((value * ((1U << bits) - 1) + 127) / 255) << shift;
}
/* Center each filtered sample on its destination pixel to retain thin text
 * and icon strokes when the logical desktop is reduced to fit a display. */
static u32 surface_sample(u32 position, u32 source, u32 destination) {
    if (destination >= source) return (position * source / destination) << 8;
    int sample = (int)((position * 256 + 128) * source / destination) - 128;
    if (sample < 0) return 0;
    u32 limit = (source - 1) << 8;
    return (u32)sample < limit ? (u32)sample : limit;
}
static u32 surface_mix(u32 first, u32 second, u32 fraction) {
    u32 inverse = 256 - fraction;
    u32 red_blue = (first & 0xff00ff) * inverse + (second & 0xff00ff) * fraction;
    u32 green = (first & 0x00ff00) * inverse + (second & 0x00ff00) * fraction;
    return ((red_blue >> 8) & 0xff00ff) | ((green >> 8) & 0x00ff00);
}
static void surface_present(const MaySurface *s, const u32 *pixels, int scale_mode) {
    if ((scale_mode == 0 || scale_mode == 2) &&
        s->width == W && s->height == H && s->bpp == 32 &&
        s->red_bits == 8 && s->red == 16 && s->green_bits == 8 && s->green == 8 &&
        s->blue_bits == 8 && s->blue == 0 &&
        (!s->alpha_bits || (s->alpha_bits == 8 && s->alpha == 24))) {
        u32 alpha = s->alpha_bits ? 0xff000000U : 0;
        for (u32 y = 0; y < H; y++) {
            u32 *row = (u32 *)(s->data + (s->yoffset + y) * s->pitch + s->xoffset * 4);
            for (u32 x = 0; x < W; x++) row[x] = pixels[y * W + x] | alpha;
        }
        return;
    }
    u32 dw, dh;
    if (scale_mode == 1) {
        dw = W; dh = H;
    } else if (scale_mode == 3) {
        dw = s->width; dh = dw * H / W;
        if (dh < s->height) { dh = s->height; dw = dh * W / H; }
    } else {
        dw = s->width; dh = dw * H / W;
        if (dh > s->height) { dh = s->height; dw = dh * W / H; }
    }
    i32 left = ((i32)s->width - (i32)dw) / 2;
    i32 top = ((i32)s->height - (i32)dh) / 2;
    u32 bytes = s->bpp / 8;
    static u32 xmap[16384], ymap[16384];
    for (u32 x = 0; x < s->width; x++) {
        i32 relative = (i32)x - left;
        xmap[x] = relative < 0 || (u32)relative >= dw ? 0xffffffffU : surface_sample((u32)relative, W, dw);
    }
    for (u32 y = 0; y < s->height; y++) {
        i32 relative = (i32)y - top;
        ymap[y] = relative < 0 || (u32)relative >= dh ? 0xffffffffU : surface_sample((u32)relative, H, dh);
    }
    for (u32 y = 0; y < s->height; y++) {
        u8 *row = s->data + (s->yoffset + y) * s->pitch + s->xoffset * bytes;
        for (u32 x = 0; x < s->width; x++) {
            u32 color = 0;
            if (xmap[x] != 0xffffffffU && ymap[y] != 0xffffffffU) {
                u32 xf = xmap[x] & 255, yf = ymap[y] & 255;
                const u32 *source = pixels + (ymap[y] >> 8) * W + (xmap[x] >> 8);
                color = source[0];
                if (xf) color = surface_mix(color, source[1], xf);
                if (yf) {
                    u32 next = source[W];
                    if (xf) next = surface_mix(next, source[W + 1], xf);
                    color = surface_mix(color, next, yf);
                }
            }
            u32 packed = surface_channel(color >> 16 & 255, s->red_bits, s->red) |
                         surface_channel(color >> 8 & 255, s->green_bits, s->green) |
                         surface_channel(color & 255, s->blue_bits, s->blue);
            if (s->alpha_bits) packed |= ((1U << s->alpha_bits) - 1) << s->alpha;
            for (u32 byte = 0; byte < bytes; byte++) row[x * bytes + byte] = packed >> (byte * 8);
        }
    }
}
#endif

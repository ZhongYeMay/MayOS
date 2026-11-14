#ifndef MAYOS_FRAMEBUFFER_H
#define MAYOS_FRAMEBUFFER_H
#include "core.h"
/* Standard firmware framebuffer adapter, shared by both kernels.
 * Native logical framebuffer is 1280x720 (16:9), with aspect-safe scaling
 * on fallback firmware modes rather than stretched desktop pixels. */
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
static void surface_present(const MaySurface *s, const u32 *pixels) {
    if (s->width == W && s->height == H && s->bpp == 32 &&
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
    u32 dw = s->width, dh = dw * H / W;
    if (dh > s->height) { dh = s->height; dw = dh * W / H; }
    u32 left = (s->width - dw) / 2, top = (s->height - dh) / 2, bytes = s->bpp / 8;
    static u32 xmap[16384], ymap[16384];
    for (u32 x = 0; x < s->width; x++) xmap[x] = x < left || x >= left + dw ? 0xffffffffU : (x - left) * W / dw;
    for (u32 y = 0; y < s->height; y++) ymap[y] = y < top || y >= top + dh ? 0xffffffffU : (y - top) * H / dh * W;
    for (u32 y = 0; y < s->height; y++) {
        u8 *row = s->data + (s->yoffset + y) * s->pitch + s->xoffset * bytes;
        for (u32 x = 0; x < s->width; x++) {
            u32 color = 0;
            if (xmap[x] != 0xffffffffU && ymap[y] != 0xffffffffU)
                color = pixels[ymap[y] + xmap[x]];
            u32 packed = surface_channel(color >> 16 & 255, s->red_bits, s->red) |
                         surface_channel(color >> 8 & 255, s->green_bits, s->green) |
                         surface_channel(color & 255, s->blue_bits, s->blue);
            if (s->alpha_bits) packed |= ((1U << s->alpha_bits) - 1) << s->alpha;
            for (u32 byte = 0; byte < bytes; byte++) row[x * bytes + byte] = packed >> (byte * 8);
        }
    }
}
#endif

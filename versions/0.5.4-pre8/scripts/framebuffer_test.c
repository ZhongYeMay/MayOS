#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAYOS_LINUX
#include "../kernel/framebuffer.h"
#undef memcpy
#undef memset
#undef strlen
#undef strcmp
#undef reboot

static void run(u32 width, u32 height, u32 bpp, int bgr) {
    u32 bytes = bpp / 8, stride = (width + 3) * bytes + 8;
    size_t capacity = (size_t)(height + 2) * stride;
    u8 *memory = malloc(capacity + 64);
    u32 *pixels = malloc(W * H * sizeof(*pixels));
    assert(memory && pixels);
    memset(memory, 0xa5, capacity + 64);
    for (int i = 0; i < W * H; i++) pixels[i] = 0x00ff0000;
    MaySurface s = {.data=memory + 32, .width=width, .height=height, .pitch=stride,
        .bpp=bpp, .xoffset=1, .yoffset=1,
        .red=bpp == 16 ? 11 : (bgr ? 0 : 16), .red_bits=bpp == 16 ? 5 : 8,
        .green=bpp == 16 ? 5 : 8, .green_bits=bpp == 16 ? 6 : 8,
        .blue=bpp == 16 ? 0 : (bgr ? 16 : 0), .blue_bits=bpp == 16 ? 5 : 8};
    assert(surface_valid(&s, capacity));
    assert(!surface_valid(&s, (u64)height * stride));
    surface_present(&s, pixels, 0);
    u32 expected = ((1U << s.red_bits) - 1) << s.red;
    u32 dw = width, dh = width * H / W;
    if (dh > height) { dh = height; dw = dh * W / H; }
    u32 left = (width - dw) / 2, top = (height - dh) / 2;
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 value = 0;
            for (u32 byte = 0; byte < bytes; byte++)
                value |= (u32)s.data[(y + 1) * stride + (x + 1) * bytes + byte] << (byte * 8);
            int inside = x >= left && x < left + dw && y >= top && y < top + dh;
            assert(value == (inside ? expected : 0));
        }
        for (u32 n = (width + 1) * bytes; n < stride; n++) assert(s.data[(y + 1) * stride + n] == 0xa5);
        for (u32 n = 0; n < bytes; n++) assert(s.data[(y + 1) * stride + n] == 0xa5);
    }
    for (int i = 0; i < 32; i++) assert(memory[i] == 0xa5 && memory[capacity + 32 + i] == 0xa5);
    MaySurface invalid = s;
    invalid.green = invalid.red; assert(!surface_valid(&invalid, capacity));
    invalid = s; invalid.xoffset = 0xffffffffU; assert(!surface_valid(&invalid, capacity));
    invalid = s; invalid.bpp = 8; assert(!surface_valid(&invalid, capacity));
    /* A touch pointer at the center must hit the logical center, even with
     * bars/cropping on non-16:9 and portrait displays. */
    for (int mode = 0; mode <= 3; mode++) {
        int x = surface_screen_to_desktop(&s, mode, width / 2, 0);
        int y = surface_screen_to_desktop(&s, mode, height / 2, 1);
        assert(x >= W / 2 - 2 && x <= W / 2 + 2);
        assert(y >= H / 2 - 2 && y <= H / 2 + 2);
        assert(surface_screen_to_desktop(&s, mode, 0, 0) >= 0);
        assert(surface_screen_to_desktop(&s, mode, height - 1, 1) < H);
    }
    free(pixels); free(memory);
}
int main(void) {
    assert(surface_sample(0, 1280, 1024) == 32);
    assert(surface_sample(1023, 1280, 1024) == (1279 << 8) - 32);
    assert(surface_sample(15, 1280, 1280) == 15 << 8);
    assert(surface_mix(0xffffff, 0, 128) == 0x7f7f7f);
    assert(surface_mix(0xff0000, 0x0000ff, 128) == 0x7f007f);
    for (int bpp = 16; bpp <= 32; bpp += 8) {
        run(640, 480, bpp, 0); run(800, 600, bpp, 0);
        run(1024, 768, bpp, 0); run(1920, 1080, bpp, 1);
        if (bpp == 32) {
            run(1366, 768, bpp, 1); run(1280, 1024, bpp, 0);
            run(1920, 1200, bpp, 0); run(2560, 1600, bpp, 0);
            run(3440, 1440, bpp, 1); run(3840, 2160, bpp, 0);
            run(4096, 2160, bpp, 0); run(2160, 3840, bpp, 0);
        }
    }
    puts("PASS: both kernels' framebuffer adapter, resolutions, RGB565/24/32/BGR, pitch, offsets and bounds");
}

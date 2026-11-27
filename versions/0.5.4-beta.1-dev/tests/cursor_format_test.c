#include <assert.h>
#include <stdio.h>
#include "cursor.h"
#undef memset
#undef memcpy

void *mayos_memcpy(void *dst, const void *src, u32 size) { return memcpy(dst, src, size); }
void *mayos_memset(void *dst, int value, u32 size) { return memset(dst, value, size); }

static MayCursor decoded;
static u8 data[MAY_CURSOR_FILE_MAX];

static void fixture(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    u32 size = (u32)fread(data, 1, sizeof(data), file);
    assert(feof(file) && !ferror(file));
    assert(fclose(file) == 0);
    for (u32 i = 0; i < size; i++)
        assert(!may_cursor_decode(data, i, &decoded) && decoded.count == 0);
    assert(may_cursor_decode(data, size, &decoded));
}

int main(void) {
    fixture("tests/cursors/Mono.cur");
    assert(decoded.frames[0].hotspot_x == 1);
    assert(decoded.frames[0].pixels[0] == 0xff000000);
    assert(decoded.frames[0].pixels[1] == 0xffffffff);
    assert(decoded.frames[0].pixels[2] == 0);
    assert(decoded.frames[0].pixels[3] == 0x00ffffff); /* Legacy screen inversion. */

    fixture("tests/cursors/Mint.cur");
    assert(decoded.frames[0].width == 32 && decoded.frames[0].height == 32);
    assert(decoded.frames[0].hotspot_x == 8 && decoded.frames[0].hotspot_y == 7);
    assert(decoded.frames[0].pixels[0] == 0);
    assert(decoded.frames[0].pixels[7 * 32 + 8] == 0xff40e6be);

    fixture("tests/cursors/Indexed.cur");
    assert(decoded.frames[0].pixels[0] == 0);
    assert(decoded.frames[0].pixels[7 * 32 + 8] == 0xffc82837);

    fixture("tests/cursors/Pulse.ani");
    assert(decoded.count == 2 && decoded.steps == 3 && decoded.duration == 54);
    assert(may_cursor_frame(&decoded, 0) == 1);
    assert(may_cursor_frame(&decoded, 29) == 1);
    assert(may_cursor_frame(&decoded, 30) == 0);
    assert(may_cursor_frame(&decoded, 70) == 1);
    assert(may_cursor_frame(&decoded, 120) == 0);
    puts("PASS: DIB AND/XOR, PNG alpha and indexed palette, ANI timing, and truncated input");
}

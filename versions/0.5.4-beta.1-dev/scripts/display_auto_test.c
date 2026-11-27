#include <assert.h>
#include <stdio.h>
#include "../kernel/display_modes.h"
static void expect(const char *modes, unsigned width, unsigned height) {
    MayDisplayMode found = {0, 0};
    assert(may_display_first_4k(modes, &found));
    assert(found.width == width && found.height == height);
}
int main(void) {
    MayDisplayMode mode = {0, 0};
    expect("1920x1080\n3840x2160\n", 1920, 1080);
    expect("3840x2160\n1920x1080\n", 3840, 2160);
    expect("4096x2160\n", 4096, 2160);
    expect("2560x1600\n", 2560, 1600);
    expect("1600x1200\n", 1600, 1200);
    expect("1280x1024\n", 1280, 1024);
    expect("3440x1440\n", 3440, 1440);
    expect("3840x1080\n", 3840, 1080);
    expect("2160x3840\n", 2160, 3840);
    expect("1366x768\n", 1366, 768);
    expect("1080x1920\n", 1080, 1920);
    expect("5120x2160\n4096x2160\n", 4096, 2160);
    expect("7680x4320\n2560x1440\n", 2560, 1440);
    expect("5000x1200\n1024x768\n", 1024, 768);
    assert(!may_display_first_4k("bad\n8K\n99999x99999\n", &mode));
    assert(!may_display_first_4k("", &mode));
    assert(!may_display_first_4k(NULL, &mode));
    assert(!may_display_parse("1920x1080@60", &mode));
    assert(!may_display_parse("1920x-1080", &mode));
    assert(!may_display_parse("1920x1080garbage", &mode));
    assert(!may_display_parse("639x480", &mode));
    assert(!may_display_parse("1920x479", &mode));
    assert(may_display_parse("2560x1440\r\n", &mode));
    assert(mode.width == 2560 && mode.height == 1440);
    puts("PASS: preferred modes UHD/DCI/ultrawide/portrait/4:3/16:10; cap and fallback");
    return 0;
}

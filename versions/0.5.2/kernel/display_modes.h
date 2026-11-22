#ifndef MAYOS_DISPLAY_MODES_H
#define MAYOS_DISPLAY_MODES_H
/*
 * Monitor-advertised video mode policy shared by test and Linux framebuffer.
 * No fixed aspect ratio or fixed resolution table: any mode is eligible if
 * its LONGER side <=4096 and the shorter side >=480.
 * EDID/DRM mode lists conventionally put the preferred timing first, so
 * preserve display order instead of inventing a 16:9 priority.
 */
typedef struct {
    unsigned width, height;
} MayDisplayMode;

static int may_display_supported(unsigned width, unsigned height) {
    return width >= 640 && height >= 480 && width <= 4096 && height <= 4096;
}
static int may_display_parse(const char *line, MayDisplayMode *out) {
    if (!line || !out) return 0;
    unsigned width = 0, height = 0;
    const char *at = line;
    if (*at < '0' || *at > '9') return 0;
    for (; *at >= '0' && *at <= '9'; at++) {
        width = width * 10 + (unsigned)(*at - '0');
        if (width > 4096) return 0;
    }
    if (*at++ != 'x') return 0;
    if (*at < '0' || *at > '9') return 0;
    for (; *at >= '0' && *at <= '9'; at++) {
        height = height * 10 + (unsigned)(*at - '0');
        if (height > 4096) return 0;
    }
    if (*at && *at != '\n' && *at != '\r') return 0;
    if (!may_display_supported(width, height)) return 0;
    out->width = width;
    out->height = height;
    return 1;
}
static int may_display_first_4k(const char *list, MayDisplayMode *out) {
    if (!list || !out) return 0;
    /* Single-connector mode list is ordered by the GPU/EDID driver. */
    const char *line = list;
    while (*line) {
        MayDisplayMode choice;
        if (may_display_parse(line, &choice)) {
            *out = choice;
            return 1;
        }
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }
    return 0;
}
#endif

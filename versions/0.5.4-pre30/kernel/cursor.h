#ifndef MAYOS_CURSOR_H
#define MAYOS_CURSOR_H
#include "core.h"

enum { MAY_CURSOR_FILE_MAX = 262144, MAY_CURSOR_EDGE = 64, MAY_CURSOR_FRAMES = 24, MAY_CURSOR_STEPS = 96 };
typedef struct {
    u32 pixels[MAY_CURSOR_EDGE * MAY_CURSOR_EDGE];
    u16 width, height, hotspot_x, hotspot_y;
} MayCursorFrame;
typedef struct {
    MayCursorFrame frames[MAY_CURSOR_FRAMES];
    u16 step_time[MAY_CURSOR_STEPS]; /* ANI jiffies, 1/60 second. */
    u8 sequence[MAY_CURSOR_STEPS];
    u32 count, steps, duration;
} MayCursor;

int may_cursor_decode(const u8 *data, u32 size, MayCursor *out);
u32 may_cursor_frame(const MayCursor *cursor, u32 hundredths);
const u8 *platform_pointer_asset(u32 *size);

#endif

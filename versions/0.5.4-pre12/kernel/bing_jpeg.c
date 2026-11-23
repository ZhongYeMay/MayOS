/* Use stb's JPEG decoder with a private, bounded workspace. */
#include <stddef.h>
#include <string.h>

enum { WORKSPACE_SIZE = 32 * 1024 * 1024 };
static _Alignas(16) unsigned char workspace[WORKSPACE_SIZE];
static size_t used;

static void *jpeg_alloc(size_t size) {
    size_t aligned = (used + 15u) & ~(size_t)15u;
    if (aligned > sizeof workspace || size > sizeof workspace - aligned) return NULL;
    used = aligned + size;
    return workspace + aligned;
}

static void *jpeg_realloc(void *old, size_t old_size, size_t size) {
    void *address = jpeg_alloc(size);
    if (address && old) memcpy(address, old, old_size < size ? old_size : size);
    return address;
}

#define STBI_MALLOC(size) jpeg_alloc(size)
#define STBI_REALLOC_SIZED(old, old_size, size) jpeg_realloc(old, old_size, size)
#define STBI_FREE(address) ((void)(address))
#define STBI_ONLY_JPEG
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(expression) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#include "wallpaper.h"

int may_bing_decode_jpeg(const unsigned char *data, unsigned size, unsigned int *pixels) {
    if (size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
        data[size - 2] != 0xff || data[size - 1] != 0xd9) return 0;
    int width, height, channels;
    used = 0;
    if (!stbi_info_from_memory(data, (int)size, &width, &height, &channels) ||
        width < 16 || height < 16 || width > 2048 || height > 1200) return 0;
    used = 0;
    unsigned char *image = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 3);
    if (!image) return 0;

    unsigned crop_width = (unsigned)width;
    unsigned crop_height = (unsigned)height;
    if (width * MAY_WALLPAPER_HEIGHT > height * MAY_WALLPAPER_WIDTH)
        crop_width = (unsigned)height * MAY_WALLPAPER_WIDTH / MAY_WALLPAPER_HEIGHT;
    else
        crop_height = (unsigned)width * MAY_WALLPAPER_HEIGHT / MAY_WALLPAPER_WIDTH;
    unsigned left = ((unsigned)width - crop_width) / 2u;
    unsigned top = ((unsigned)height - crop_height) / 2u;
    for (unsigned y = 0; y < MAY_WALLPAPER_HEIGHT; y++) {
        const unsigned char *row = image + 3u * (unsigned)width *
                                   (top + y * crop_height / MAY_WALLPAPER_HEIGHT);
        for (unsigned x = 0; x < MAY_WALLPAPER_WIDTH; x++) {
            const unsigned char *rgb = row + 3u * (left + x * crop_width / MAY_WALLPAPER_WIDTH);
            *pixels++ = ((unsigned)rgb[0] << 16) | ((unsigned)rgb[1] << 8) | rgb[2];
        }
    }
    return 1;
}

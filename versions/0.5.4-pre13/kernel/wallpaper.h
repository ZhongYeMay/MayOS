#ifndef MAYOS_WALLPAPER_H
#define MAYOS_WALLPAPER_H

/* Fixed-size local handoff from the HTTPS/JPEG worker to the desktop. */
#define MAY_WALLPAPER_MAGIC "MAYBING1"
#define MAY_WALLPAPER_WIDTH 1280
#define MAY_WALLPAPER_HEIGHT 720
#define MAY_WALLPAPER_HEADER_BYTES 16
#define MAY_WALLPAPER_IMAGE_BYTES (MAY_WALLPAPER_WIDTH * MAY_WALLPAPER_HEIGHT * 4)
#define MAY_WALLPAPER_FILE_BYTES (MAY_WALLPAPER_HEADER_BYTES + MAY_WALLPAPER_IMAGE_BYTES)
#define MAY_WALLPAPER_PATH "/run/mayos-wallpaper.mwb"
#define MAY_WALLPAPER_WORKER "/usr/bin/mayos-bing-wallpaper"

static inline int wallpaper_date_valid(const char *date) {
    for (int i = 0; i < 8; i++)
        if (date[i] < '0' || date[i] > '9') return 0;
    return 1;
}

#endif

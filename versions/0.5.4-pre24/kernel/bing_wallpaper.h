#ifndef MAYOS_BING_WALLPAPER_H
#define MAYOS_BING_WALLPAPER_H

int may_bing_wallpaper_poll(unsigned int *pixels, char date[9], int enabled);
int may_bing_decode_jpeg(const unsigned char *data, unsigned size, unsigned int *pixels);

#endif

#ifndef SUMMER_WEBP_H
#define SUMMER_WEBP_H
#include "render.h"
/* Decodes original WebP files without modifying or recompressing them.
 * The caller owns the returned pixel buffer and releases it with summer_image_free. */
int summer_image_load_webp(const char *filename,SummerImage *out);
void summer_image_free(SummerImage *image);
#endif

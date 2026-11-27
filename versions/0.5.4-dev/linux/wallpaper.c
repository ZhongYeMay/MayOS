/* Bing wallpaper: local-use-only, no images embedded in released MayOS.
 * Strictly HTTPS Bing origin; bounded metadata/JPEG; libjpeg validates input.
 * Writes a 1280x720 little-endian RGB565 bitmap by atomic rename.
 * Exit 0 means current cache is usable; nonzero keeps yesterday's cache.
 */
#define _GNU_SOURCE
#include <curl/curl.h>
#include <jpeglib.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { WIDTH = 1280, HEIGHT = 720, OUTPUT_SIZE = WIDTH * HEIGHT * 2,
       MAX_JSON = 65536, MAX_JPEG = 12 * 1024 * 1024 };
typedef struct { unsigned char *data; size_t used, max; } Data;
typedef struct { struct jpeg_error_mgr pub; jmp_buf jump; } JpegError;

static void jpeg_fail(j_common_ptr info) {
    longjmp(((JpegError *)info->err)->jump, 1);
}
static size_t collect(char *bytes, size_t size, size_t count, void *context) {
    Data *d = context;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if (n > d->max - d->used) return 0;
    memcpy(d->data + d->used, bytes, n);
    d->used += n;
    return n;
}
static int get_bing(const char *url, Data *data) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MayOS-BingDaily/0.5.4 (personal desktop)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    return result == CURLE_OK && status == 200;
}
static int get_string(const char *json, const char *name, char *dst, size_t cap) {
    char needle[48];
    if (snprintf(needle, sizeof(needle), "\"%s\"", name) >= (int)sizeof(needle))
        return 0;
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != '"') return 0;
    size_t len = 0;
    while (*p && *p != '"') {
        /* Escapes are rejected rather than misinterpreted as a trusted URL. */
        if (*p == '\\' || (unsigned char)*p < 32 || len + 1 >= cap) return 0;
        dst[len++] = *p++;
    }
    if (*p != '"' || !len) return 0;
    dst[len] = 0;
    return 1;
}
static int validate_path(const char *path) {
    const char *prefix = "/th?id=OHR.";
    if (strncmp(path, prefix, strlen(prefix))) return 0;
    size_t length = strlen(path);
    if (length < 25 || length > 480 || !strstr(path, ".jpg") ||
        strstr(path, "..")) return 0;
    for (const char *p = path; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_' || *p == '.' || *p == '?' ||
              *p == '=' || *p == '&' || *p == '/')) return 0;
    return 1;
}
static int parse_metadata(const char *json, char path[512], char date[9]) {
    char start[32];
    if (!strstr(json, "\"images\"") ||
        !get_string(json, "url", path, 512) ||
        !get_string(json, "startdate", start, sizeof(start)) ||
        strlen(start) != 8 || !validate_path(path)) return 0;
    for (size_t i = 0; i < 8; i++)
        if (start[i] < '0' || start[i] > '9') return 0;
    memcpy(date, start, 9);
    return 1;
}
static int decode_jpeg(Data image, uint16_t *output) {
    struct jpeg_decompress_struct jpeg;
    JpegError error;
    memset(&jpeg, 0, sizeof(jpeg));
    jpeg.err = jpeg_std_error(&error.pub);
    error.pub.error_exit = jpeg_fail;
    if (setjmp(error.jump)) {
        jpeg_destroy_decompress(&jpeg);
        return 0;
    }
    jpeg_create_decompress(&jpeg);
    jpeg_mem_src(&jpeg, image.data, (unsigned long)image.used);
    if (jpeg_read_header(&jpeg, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&jpeg);
        return 0;
    }
    jpeg.out_color_space = JCS_RGB;
    jpeg_start_decompress(&jpeg);
    unsigned width = jpeg.output_width, height = jpeg.output_height;
    if (width < 640 || height < 360 || width > 8192 || height > 8192 ||
        jpeg.output_components != 3) {
        jpeg_destroy_decompress(&jpeg);
        return 0;
    }
    JSAMPARRAY row = (*jpeg.mem->alloc_sarray)((j_common_ptr)&jpeg, JPOOL_IMAGE,
                                                width * 3u, 1);
    for (unsigned y = 0; y < HEIGHT; y++) {
        unsigned wanted = (uint64_t)y * height / HEIGHT;
        while (jpeg.output_scanline <= wanted) {
            if (jpeg_read_scanlines(&jpeg, row, 1) != 1) {
                jpeg_destroy_decompress(&jpeg);
                return 0;
            }
        }
        for (unsigned x = 0; x < WIDTH; x++) {
            unsigned sx = (uint64_t)x * width / WIDTH;
            const unsigned char *c = row[0] + 3u * sx;
            output[y * WIDTH + x] =
                (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
        }
    }
    jpeg_abort_decompress(&jpeg);
    jpeg_destroy_decompress(&jpeg);
    return 1;
}
static int write_all(int fd, const void *bytes, size_t length) {
    const unsigned char *p = bytes;
    while (length) {
        ssize_t count = write(fd, p, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        p += count; length -= (size_t)count;
    }
    return 1;
}
static int atomic_write(const char *path, const void *bytes, size_t length) {
    char tmp[512];
    if (strlen(path) > sizeof(tmp) - 14 ||
        snprintf(tmp, sizeof(tmp), "%s.tmp.XXXXXX", path) >= (int)sizeof(tmp))
        return 0;
    int fd = mkstemp(tmp);
    if (fd < 0) return 0;
    int good = fchmod(fd, 0600) == 0 &&
               write_all(fd, bytes, length) && fsync(fd) == 0;
    if (close(fd)) good = 0;
    if (good && rename(tmp, path) == 0) return 1;
    unlink(tmp);
    return 0;
}
static int cache_valid(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_size == OUTPUT_SIZE;
}
static int same_date(const char *path, const char *date) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char stored[16] = {0};
    size_t got = fread(stored, 1, sizeof(stored) - 1, f);
    fclose(f);
    return got == 9 && !memcmp(stored, date, 8) && stored[8] == '\n';
}
int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) {
        fputs("Usage: mayos-wallpaper OUTPUT.r565 OUTPUT.date [--fixture-jpeg LOCAL.jpg]\n", stderr);
        return 2;
    }
    const int fixture = argc == 5 && !strcmp(argv[3], "--fixture-jpeg");
    if (argc == 5 && !fixture) return 2;
    /* Test fixture mode has no network access and is used only by CI. */
    char date[9] = "19700101";
    Data jpeg = {.data = malloc(MAX_JPEG), .used = 0, .max = MAX_JPEG};
    if (!jpeg.data) return 1;
    if (!fixture) {
        Data meta = {.data = malloc(MAX_JSON + 1), .used = 0, .max = MAX_JSON};
        if (!meta.data) { free(jpeg.data); return 1; }
        const char *api =
            "https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN";
        int ok = get_bing(api, &meta);
        meta.data[meta.used] = 0;
        char path[512], url[600];
        if (!ok || !parse_metadata((const char *)meta.data, path, date) ||
            snprintf(url, sizeof(url), "https://www.bing.com%s", path) >= (int)sizeof(url)) {
            free(meta.data); free(jpeg.data); return 1;
        }
        free(meta.data);
        if (cache_valid(argv[1]) && same_date(argv[2], date)) {
            free(jpeg.data); return 0;
        }
        if (!get_bing(url, &jpeg)) { free(jpeg.data); return 1; }
    } else {
        FILE *f = fopen(argv[4], "rb");
        if (!f) { free(jpeg.data); return 1; }
        jpeg.used = fread(jpeg.data, 1, jpeg.max, f);
        int extra = fgetc(f);
        fclose(f);
        if (extra != EOF || !jpeg.used) { free(jpeg.data); return 1; }
    }
    uint16_t *pixels = malloc(OUTPUT_SIZE);
    if (!pixels) { free(jpeg.data); return 1; }
    int ok = decode_jpeg(jpeg, pixels);
    free(jpeg.data);
    if (ok) ok = atomic_write(argv[1], pixels, OUTPUT_SIZE);
    free(pixels);
    if (ok) {
        char day[10];
        memcpy(day, date, 8); day[8] = '\n'; day[9] = 0;
        ok = atomic_write(argv[2], day, 9);
    }
    if (ok) printf("MayOS Bing wallpaper: %s; cached RGB565 %dx%d\n", date, WIDTH, HEIGHT);
    return ok ? 0 : 1;
}

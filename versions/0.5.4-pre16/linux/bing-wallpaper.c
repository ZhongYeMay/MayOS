/* A separate worker keeps network and image decoding off the UI thread. */
#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <json-c/json.h>

#include "../kernel/wallpaper.h"

#define BING_ORIGIN "https://www.bing.com"
#define BING_ARCHIVE BING_ORIGIN "/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=en-US"
#define METADATA_LIMIT (32 * 1024)
#define JPEG_LIMIT (8 * 1024 * 1024)

struct blob {
    unsigned char *data;
    size_t size, limit;
};

static size_t append(char *data, size_t size, size_t count, void *context) {
    struct blob *body = context;
    if (!size || count > (body->limit - body->size) / size) return 0;
    size_t length = size * count;
    memcpy(body->data + body->size, data, length);
    body->size += length;
    return length;
}

static int request(const char *url, struct blob *body) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;
    long status = 0;
#define SET(option, value) if (curl_easy_setopt(curl, option, value) != CURLE_OK) goto done
    SET(CURLOPT_URL, url);
    SET(CURLOPT_PROTOCOLS_STR, "https");
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
    SET(CURLOPT_CONNECTTIMEOUT, 8L);
    SET(CURLOPT_TIMEOUT, 25L);
    SET(CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)body->limit);
    SET(CURLOPT_USERAGENT, "MayOS Bing Wallpaper/1");
    SET(CURLOPT_WRITEFUNCTION, append);
    SET(CURLOPT_WRITEDATA, body);
#undef SET
    CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    else fprintf(stderr, "Bing Wallpaper transfer: %s\n", curl_easy_strerror(result));
done:
    curl_easy_cleanup(curl);
    return status == 200 && body->size > 0;
}

static int safe_image_path(const char *path) {
    if (strncmp(path, "/th?id=OHR.", 11)) return 0;
    for (; *path; path++) {
        unsigned char c = *path;
        int alphanumeric = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9');
        if (!alphanumeric && !strchr("/?=._&-%", c)) return 0;
    }
    return 1;
}

static int metadata(const struct blob *body, char date[9], char url[1024]) {
    struct json_tokener *parser = json_tokener_new();
    if (!parser) return 0;
    json_tokener_set_flags(parser, JSON_TOKENER_VALIDATE_UTF8);
    body->data[body->size] = 0;
    struct json_object *root = json_tokener_parse_ex(parser, (const char *)body->data, body->size + 1);
    int ok = 0;
    struct json_object *images, *entry, *key, *image, *available;
    if (json_tokener_get_error(parser) == json_tokener_success && root &&
        json_object_object_get_ex(root, "images", &images) &&
        json_object_is_type(images, json_type_array) &&
        (entry = json_object_array_get_idx(images, 0)) &&
        json_object_object_get_ex(entry, "wp", &available) &&
        json_object_is_type(available, json_type_boolean) && json_object_get_boolean(available) &&
        json_object_object_get_ex(entry, "startdate", &key) &&
        json_object_is_type(key, json_type_string) && json_object_get_string_len(key) == 8 &&
        wallpaper_date_valid(json_object_get_string(key)) &&
        json_object_object_get_ex(entry, "url", &image) &&
        json_object_is_type(image, json_type_string) &&
        strlen(json_object_get_string(image)) == (size_t)json_object_get_string_len(image) &&
        safe_image_path(json_object_get_string(image))) {
        int length = snprintf(url, 1024, "%s%s", BING_ORIGIN, json_object_get_string(image));
        if (length > 0 && length < 1024) {
            memcpy(date, json_object_get_string(key), 8);
            date[8] = 0;
            ok = 1;
        }
    }
    if (root) json_object_put(root);
    json_tokener_free(parser);
    return ok;
}

struct jpeg_error {
    struct jpeg_error_mgr base;
    jmp_buf jump;
};

static void bad_jpeg(j_common_ptr decoder) {
    struct jpeg_error *error = (struct jpeg_error *)decoder->err;
    longjmp(error->jump, 1);
}

static void jpeg_message(j_common_ptr decoder, int level) {
    if (level < 0) bad_jpeg(decoder);
}

static int decode(const struct blob *body, uint32_t *pixels) {
    struct jpeg_decompress_struct *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) return 0;
    struct jpeg_error error;
    decoder->err = jpeg_std_error(&error.base);
    error.base.error_exit = bad_jpeg;
    error.base.emit_message = jpeg_message;
    volatile int success = 0;
    if (setjmp(error.jump)) goto done;
    jpeg_create_decompress(decoder);
    jpeg_mem_src(decoder, body->data, body->size);
    if (jpeg_read_header(decoder, TRUE) != JPEG_HEADER_OK ||
        decoder->image_width < 64 || decoder->image_height < 64 ||
        decoder->image_width > 4096 || decoder->image_height > 4096) goto done;
    decoder->out_color_space = JCS_RGB;
    jpeg_start_decompress(decoder);
    if (decoder->output_components != 3) goto done;
    unsigned width = decoder->output_width, height = decoder->output_height;
    unsigned crop_width = width, crop_height = height;
    if (width * MAY_WALLPAPER_HEIGHT > height * MAY_WALLPAPER_WIDTH)
        crop_width = height * MAY_WALLPAPER_WIDTH / MAY_WALLPAPER_HEIGHT;
    else
        crop_height = width * MAY_WALLPAPER_HEIGHT / MAY_WALLPAPER_WIDTH;
    unsigned left = (width - crop_width) / 2, top = (height - crop_height) / 2;
    JSAMPARRAY rows = decoder->mem->alloc_sarray((j_common_ptr)decoder, JPOOL_IMAGE, width * 3, 1);
    unsigned output_y = 0;
    while (decoder->output_scanline < height) {
        unsigned source_y = decoder->output_scanline;
        if (jpeg_read_scanlines(decoder, rows, 1) != 1) goto done;
        while (output_y < MAY_WALLPAPER_HEIGHT &&
               source_y == top + output_y * crop_height / MAY_WALLPAPER_HEIGHT) {
            uint32_t *output = pixels + output_y * MAY_WALLPAPER_WIDTH;
            for (unsigned x = 0; x < MAY_WALLPAPER_WIDTH; x++) {
                const unsigned char *rgb = rows[0] + (left + x * crop_width / MAY_WALLPAPER_WIDTH) * 3;
                output[x] = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | rgb[2];
            }
            output_y++;
        }
    }
    success = output_y == MAY_WALLPAPER_HEIGHT && jpeg_finish_decompress(decoder);
done:
    jpeg_destroy_decompress(decoder);
    free(decoder);
    return success;
}

static int have_wallpaper(const char *path, const char date[9]) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    struct stat statbuf;
    char header[MAY_WALLPAPER_HEADER_BYTES];
    int ok = !fstat(fileno(file), &statbuf) && S_ISREG(statbuf.st_mode) &&
        statbuf.st_size == MAY_WALLPAPER_FILE_BYTES &&
        fread(header, 1, sizeof(header), file) == sizeof(header) &&
        !memcmp(header, MAY_WALLPAPER_MAGIC, 8) && !memcmp(header + 8, date, 8);
    fclose(file);
    return ok;
}

static int publish(const char *path, const char date[9], const uint32_t *pixels) {
    char temporary[1024];
    int length = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) return 0;
    int fd = mkstemp(temporary);
    if (fd < 0) return 0;
    FILE *file = fdopen(fd, "wb");
    int ok = 0;
    if (file) {
        ok = fwrite(MAY_WALLPAPER_MAGIC, 1, 8, file) == 8 &&
             fwrite(date, 1, 8, file) == 8 &&
             fwrite(pixels, 1, MAY_WALLPAPER_IMAGE_BYTES, file) == MAY_WALLPAPER_IMAGE_BYTES;
        if (fclose(file)) ok = 0;
    } else close(fd);
    if (!ok || rename(temporary, path)) {
        unlink(temporary);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 2 || curl_global_init(CURL_GLOBAL_DEFAULT)) return 1;
    const char *path = argc == 2 ? argv[1] : MAY_WALLPAPER_PATH;
    struct blob body = {malloc(JPEG_LIMIT), 0, METADATA_LIMIT};
    uint32_t *pixels = NULL;
    char date[9], url[1024];
    int ok = body.data && request(BING_ARCHIVE, &body);
    if (ok && !metadata(&body, date, url)) {
        fputs("Bing Wallpaper: invalid daily metadata\n", stderr);
        ok = 0;
    }
    if (ok && !have_wallpaper(path, date)) {
        body.size = 0;
        body.limit = JPEG_LIMIT;
        pixels = malloc(MAY_WALLPAPER_IMAGE_BYTES);
        ok = pixels && request(url, &body) && decode(&body, pixels) && publish(path, date, pixels);
    }
    free(pixels);
    free(body.data);
    curl_global_cleanup();
    return ok ? 0 : 1;
}

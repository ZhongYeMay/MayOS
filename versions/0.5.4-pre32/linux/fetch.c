/* MayOS native Linux transport worker. Desktop starts this ELF directly (no shell).
 * TLS/HTTP framing and DNS are handled by libcurl and its audited TLS backend. */
#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_LIMIT 1800
typedef struct { size_t used; int truncated; } Preview;
static size_t preview(void *data, size_t size, size_t count, void *opaque) {
    Preview *p = opaque;
    size_t length = size * count;
    unsigned char *bytes = data;
    for (size_t i = 0; i < length; i++) {
        if (p->used == PREVIEW_LIMIT) { p->truncated = 1; return 0; }
        unsigned char c = bytes[i];
        if (putchar(c == '\n' || (c >= 32 && c < 127) ? c : ' ') == EOF) return 0;
        p->used++;
    }
    return length;
}

typedef struct {
    FILE *file;
    EVP_MD_CTX *digest;
    unsigned long total, received;
    int percent;
} Media;

static size_t write_media(void *data, size_t size, size_t count, void *opaque) {
    Media *media = opaque;
    if (count && size > (media->total - media->received) / count) return 0;
    size_t bytes = size * count;
    if (fwrite(data, 1, bytes, media->file) != bytes ||
        EVP_DigestUpdate(media->digest, data, bytes) != 1) return 0;
    media->received += bytes;
    int percent = (int)(100 * media->received / media->total);
    if (percent != media->percent) {
        printf("@DOWNLOAD %d\n", percent);
        fflush(stdout);
        media->percent = percent;
    }
    return bytes;
}

static int release_asset(const char *url, int kernel) {
    const char *prefix = "https://raw.githubusercontent.com/ZhongYeMay/MayOS/";
    size_t size = strlen(prefix);
    if (strncmp(url, prefix, size)) return 0;
    url += size;
    if (strspn(url, "0123456789abcdef") != 40 || url[40] != '/') return 0;
    url += 41;
    size = strlen(url);
    if (size <= 4 || size >= 96 ||
        strspn(url, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-_") != size)
        return 0;
    if (!kernel) return !strcmp(url + size - 4, ".iso");
    const char suffix[] = "-MayKernel64-x86_64.elf";
    return size > sizeof(suffix) && !strncmp(url, "MayOS-", 6) &&
           !strcmp(url + size - sizeof(suffix) + 1, suffix);
}

static int download_media(const char *url, const char *expected, const char *size, int kernel) {
    char *end;
    unsigned long bytes = strtoul(size, &end, 10);
    if (!release_asset(url, kernel) || strlen(expected) != 64 ||
        strspn(expected, "0123456789abcdef") != 64 || !bytes || *end || bytes > (kernel ? 24ul : 512ul) * 1024 * 1024) {
        puts("@FAILED Invalid release media metadata");
        return 1;
    }
    const char *destination = kernel ? "/run/mayos-update.elf" : "/tmp/MayOS-update.iso";
    char temporary[32];
    snprintf(temporary, sizeof(temporary), "%s/.mayos-update-XXXXXX", kernel ? "/run" : "/tmp");
    int fd = mkstemp(temporary);
    if (fd < 0) {
        puts("@FAILED Could not allocate update storage");
        return 1;
    }
    FILE *file = fdopen(fd, "wb");
    CURL *curl = curl_easy_init();
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    Media media = {file, ctx, bytes, 0, -1};
    unsigned char digest[32];
    unsigned digest_size = 0;
    int ok = file && curl && ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 &&
        curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 180000L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt") == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)bytes) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_media) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &media) == CURLE_OK &&
        curl_easy_perform(curl) == CURLE_OK && media.received == bytes &&
        EVP_DigestFinal_ex(ctx, digest, &digest_size) == 1 && digest_size == 32;
    if (ok) {
        char computed[65];
        for (int i = 0; i < 32; i++) snprintf(computed + i * 2, 3, "%02x", digest[i]);
        ok = !strcmp(computed, expected) && !fflush(file) && !fsync(fd);
    }
    if (file) {
        if (fclose(file)) ok = 0;
    } else close(fd);
    if (ok && link(temporary, destination)) ok = 0;
    unlink(temporary);
    if (curl) curl_easy_cleanup(curl);
    EVP_MD_CTX_free(ctx);
    if (ok && !kernel) puts("@SAVED /tmp/MayOS-update.iso");
    if (!ok) puts("@FAILED Download, integrity or storage check failed");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) return 1;
    if (argc == 5 && !strcmp(argv[1], "--download"))
        return download_media(argv[2], argv[3], argv[4], 0);
    if (argc == 5 && !strcmp(argv[1], "--kernel"))
        return download_media(argv[2], argv[3], argv[4], 1);
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    if (argc == 2 && !strcmp(argv[1], "--capabilities")) {
        printf("libcurl=%s TLS=%s IPv6=%s HTTP2=%s\n", v->version,
               v->ssl_version ? v->ssl_version : "none",
               v->features & CURL_VERSION_IPV6 ? "yes" : "no",
               v->features & CURL_VERSION_HTTP2 ? "yes" : "no");
        return !(v->features & CURL_VERSION_SSL) || !(v->features & CURL_VERSION_HTTP2) ||
               !(v->features & CURL_VERSION_IPV6);
    }
    if (argc != 2 || (strncmp(argv[1], "https://", 8) && strncmp(argv[1], "http://", 7))) {
        fputs("Expected one http:// or https:// URL.\n", stdout);
        return 2;
    }
    CURLU *url = curl_url();
    if (!url || curl_url_set(url, CURLUPART_URL, argv[1], 0)) {
        puts("Invalid HTTP(S) URL.");
        return 2;
    }
    char *user = NULL;
    if (curl_url_get(url, CURLUPART_USER, &user, 0) == CURLUE_OK) {
        curl_free(user); curl_url_cleanup(url);
        puts("Credentials in URLs are not supported.");
        return 2;
    }
    curl_url_cleanup(url);
    CURL *curl = curl_easy_init();
    if (!curl) return 1;
    Preview state = {0};
    char error[CURL_ERROR_SIZE] = {0};
    const char *ca = getenv("MAYOS_CA_BUNDLE");
    if (!ca || !*ca) ca = "/etc/ssl/certs/ca-certificates.crt";
#define SET(option, value) do { if (curl_easy_setopt(curl, option, value) != CURLE_OK) { \
    puts("Transport option unavailable."); curl_easy_cleanup(curl); return 1; } } while (0)
    SET(CURLOPT_URL, argv[1]);
    SET(CURLOPT_PROTOCOLS_STR, "http,https");
    SET(CURLOPT_REDIR_PROTOCOLS_STR, !strncmp(argv[1], "https://", 8) ? "https" : "http,https");
    SET(CURLOPT_FOLLOWLOCATION, 1L);
    SET(CURLOPT_MAXREDIRS, 5L);
    SET(CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    SET(CURLOPT_TIMEOUT_MS, 8000L);
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_CAINFO, ca);
    SET(CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    SET(CURLOPT_ACCEPT_ENCODING, "");
    SET(CURLOPT_USERAGENT, "MayOS-Explorer/modern-transport");
    SET(CURLOPT_WRITEFUNCTION, preview);
    SET(CURLOPT_WRITEDATA, &state);
    SET(CURLOPT_ERRORBUFFER, error);
    SET(CURLOPT_FAILONERROR, 1L);
    CURLcode result = curl_easy_perform(curl);
    long status = 0, protocol = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(curl, CURLINFO_HTTP_VERSION, &protocol);
    if (state.truncated) puts("\n[Preview truncated at 1800 bytes]");
    else if (result != CURLE_OK) printf("\nNetwork error: %s\n", error[0] ? error : curl_easy_strerror(result));
    printf("\nHTTP status: %ld | Protocol: %s\n", status,
           protocol == CURL_HTTP_VERSION_2_0 ? "HTTP/2" : "HTTP/1.x");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return state.truncated || result == CURLE_OK ? 0 : 1;
}

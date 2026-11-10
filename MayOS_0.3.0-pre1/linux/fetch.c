/* MayOS native Linux transport worker. Desktop starts this ELF directly (no shell).
 * TLS/HTTP framing and DNS are handled by libcurl and its audited TLS backend. */
#include <curl/curl.h>
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

int main(int argc, char **argv) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) return 1;
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

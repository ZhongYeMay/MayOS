#include "core.h"
#include "bing_wallpaper.h"
#include "net.h"
#include "net_stream_io.h"
/* Keep BearSSL's size_t declarations separate from the kernel byte helpers. */
#define memcpy maybear_memcpy
#define memset maybear_memset
#define strlen maybear_strlen
#include "security_tls_stream.h"
#undef strlen
#undef memset
#undef memcpy
#include "security_trusted_time.h"
#include "wallpaper.h"

#define JSMN_STATIC
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "../third_party/jsmn.h"

static const char host[] = "www.bing.com";
static const char archive[] = "/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=en-US";

#ifdef MAYOS_BING_DIAGNOSTIC
enum { TRY_AGAIN = 400, CHECK_AGAIN = 400 };
#else
enum { TRY_AGAIN = 30000, CHECK_AGAIN = 360000 };
#endif
enum { METADATA_LIMIT = 8192, IMAGE_LIMIT = 2 * 1024 * 1024, DEADLINE = 4500 };
enum Phase { IDLE, RESOLVE, CONNECT, TRANSFER, DRAIN };
static enum Phase phase;
static int was_enabled, want_image;
static u32 next, started, received, length, sent, head_size;
static u8 address[4], content[IMAGE_LIMIT];
static char candidate[9], path[512], request[704], headers[8192];

static int same(const char *left, const char *right, unsigned length) {
    for (unsigned i = 0; i < length; i++)
        if (left[i] != right[i]) return 0;
    return 1;
}

static int equal_token(const jsmntok_t *token, const char *str) {
    return token->end - token->start == (int)strlen(str) &&
           same((const char *)content + token->start, str, strlen(str));
}

static int field(const jsmntok_t *tokens, int count, int parent, const char *key) {
    int result = -1;
    for (int i = parent + 1; i + 1 < count && tokens[i].start < tokens[parent].end; i++) {
        if (tokens[i].parent != parent || tokens[i].type != JSMN_STRING ||
            !equal_token(&tokens[i], key)) continue;
        if (result >= 0 || tokens[i + 1].parent != i) return -1;
        result = i + 1;
    }
    return result;
}

static int read_metadata(void) {
    static jsmntok_t tokens[256];
    jsmn_parser parser;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, (const char *)content, received, tokens, 256);
    if (count < 1 || tokens[0].type != JSMN_OBJECT || tokens[count - 1].end > tokens[0].end)
        return 0;
    int images = field(tokens, count, 0, "images");
    if (images < 0 || images + 1 >= count || tokens[images].type != JSMN_ARRAY) return 0;
    int entry = images + 1;
    if (tokens[entry].type != JSMN_OBJECT || tokens[entry].parent != images) return 0;
    int date = field(tokens, count, entry, "startdate");
    int url = field(tokens, count, entry, "url");
    int available = field(tokens, count, entry, "wp");
    if (date < 0 || url < 0 || available < 0 || tokens[date].type != JSMN_STRING ||
        tokens[url].type != JSMN_STRING || tokens[available].type != JSMN_PRIMITIVE ||
        !equal_token(&tokens[available], "true")) return 0;
    const char *stamp = (const char *)content + tokens[date].start;
    const char *route = (const char *)content + tokens[url].start;
    unsigned size = (unsigned)(tokens[url].end - tokens[url].start);
    if (tokens[date].end - tokens[date].start != 8 || !wallpaper_date_valid(stamp) ||
        size <= 11 || size >= sizeof path || !same(route, "/th?id=OHR.", 11)) return 0;
    for (unsigned i = 0; i < size; i++) {
        char c = route[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '?' || c == '=' || c == '.' || c == '_' ||
            c == '&' || c == '-' || c == '%') continue;
        return 0;
    }
    memcpy(candidate, stamp, 8);
    candidate[8] = 0;
    memcpy(path, route, size);
    path[size] = 0;
    return 1;
}

static char lowercase(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static int header_is(const char *value, unsigned size, const char *expected) {
    if (size != strlen(expected)) return 0;
    for (unsigned i = 0; i < size; i++)
        if (lowercase(value[i]) != expected[i]) return 0;
    return 1;
}

static int parse_headers(void) {
    const char *line = headers;
    const char *end = headers + head_size;
    if (head_size < 16 || (!same(line, "HTTP/1.0 200", 12) && !same(line, "HTTP/1.1 200", 12)) ||
        line[12] != ' ') return 0;
    while (line + 1 < end && !same(line, "\r\n", 2)) line++;
    if (line + 1 >= end) return 0;
    line += 2;
    unsigned limit = want_image ? IMAGE_LIMIT : METADATA_LIMIT;
    while (line + 1 < end && !same(line, "\r\n", 2)) {
        if (*line == ' ' || *line == '\t') return 0;
        const char *colon = line;
        while (colon < end && *colon != ':' && *colon != '\r' && *colon != '\n') colon++;
        if (colon == end || colon == line || *colon != ':') return 0;
        const char *value = colon + 1, *last = value;
        while (last < end && *last != '\r' && *last != '\n') last++;
        if (last + 1 >= end || !same(last, "\r\n", 2)) return 0;
        const char *following = last + 2;
        while (value < last && (*value == ' ' || *value == '\t')) value++;
        while (last > value && (last[-1] == ' ' || last[-1] == '\t')) last--;
        unsigned key_size = (unsigned)(colon - line);
        if (header_is(line, key_size, "content-length")) {
            if (length || value == last) return 0;
            for (; value < last; value++) {
                if (*value < '0' || *value > '9') return 0;
                length = length * 10u + (unsigned)(*value - '0');
                if (length > limit) return 0;
            }
            if (!length) return 0;
        } else if (header_is(line, key_size, "transfer-encoding") ||
                   (header_is(line, key_size, "content-encoding") &&
                    !header_is(value, (unsigned)(last - value), "identity"))) {
            return 0;
        }
        line = following;
    }
    return line + 2 == end && length != 0;
}

static int receive(const u8 *data, unsigned size) {
    while (!length && size) {
        if (head_size == sizeof headers) return 0;
        headers[head_size++] = (char)*data++;
        size--;
        if (head_size >= 4 && same(headers + head_size - 4, "\r\n\r\n", 4) && !parse_headers())
            return 0;
    }
    if (size > length - received) return 0;
    memcpy(content + received, data, size);
    received += size;
    return 1;
}

static void finish(u32 delay) {
    phase = IDLE;
    next = ticks + delay;
}

static void reset_request(const char *target) {
    const char *parts[] = { "GET ", target, " HTTP/1.1\r\nHost: ", host,
                          "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n" };
    unsigned end = 0;
    for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        unsigned size = strlen(parts[i]);
        memcpy(request + end, parts[i], size);
        end += size;
    }
    request[end] = 0;
    head_size = length = received = sent = 0;
}

#ifdef MAYOS_BING_DIAGNOSTIC
#include "tls_x509_fixture.h"
static br_x509_trust_anchor fixture_anchor = {
    .dn = {mayos_test_ca_dn, sizeof mayos_test_ca_dn},
    .flags = BR_X509_TA_CA,
    .pkey = { .key_type = BR_KEYTYPE_RSA, .key.rsa = {
        mayos_test_ca_rsa_modulus, sizeof mayos_test_ca_rsa_modulus,
        mayos_test_ca_rsa_exponent, sizeof mayos_test_ca_rsa_exponent
    } }
};
#endif

static int open_request(void) {
    int opened;
#ifdef MAYOS_BING_DIAGNOSTIC
    unsigned days, seconds;
    const u8 loopback[4] = { 10, 0, 2, 2 };
    (void)mayos_test_leaf_der;
    opened = may_time_read(&days, &seconds) &&
             may_tls_stream_open(loopback, MAYOS_BING_DIAGNOSTIC, host,
                                 &fixture_anchor, 1, days, seconds, 0, 0);
#else
    opened = may_tls_public_open(address, 443, host);
#endif
    if (!opened) return 0;
    reset_request(want_image ? path : archive);
    started = ticks;
    phase = TRANSFER;
    return 1;
}

static void stop_transfer(void) {
    if (phase == TRANSFER || phase == DRAIN) may_tls_stream_abort();
}

int may_bing_wallpaper_poll(u32 *pixels, char date[9], int enabled) {
    if (!enabled) {
        if (was_enabled) {
            stop_transfer();
            finish(0);
        }
        was_enabled = 0;
        return 0;
    }
    if (!was_enabled) next = ticks;
    was_enabled = 1;

    switch (phase) {
    case IDLE: {
        unsigned days, seconds;
        if ((i32)(ticks - next) < 0 || !may_time_read(&days, &seconds) ||
            may_net_dhcp_info()->state != MAY_DHCP_BOUND) break;
        want_image = 0;
#ifdef MAYOS_BING_DIAGNOSTIC
        phase = CONNECT;
        started = ticks;
#else
        if (may_net_dns_lookup(host)) {
            phase = RESOLVE;
            started = ticks;
        } else {
            finish(100);
        }
#endif
        break;
    }
    case RESOLVE: {
        int result = may_net_dns_result(host, address);
        if (result > 0) {
            phase = CONNECT;
            started = ticks;
        } else if (result < 0 || ticks - started >= 550u) {
            finish(TRY_AGAIN);
        }
        break;
    }
    case CONNECT:
        if (!open_request() && ticks - started >= 500u) finish(TRY_AGAIN);
        break;
    case TRANSFER: {
        u8 data[2048];
        may_tls_stream_poll();
        unsigned request_size = strlen(request);
        if (sent < request_size) sent += (unsigned)may_tls_stream_write(
            (const u8 *)request + sent, request_size - sent);
        unsigned bytes = (unsigned)may_tls_stream_read(data, sizeof data);
        int tls_state = may_tls_stream_state();
        if ((bytes && !receive(data, bytes)) || tls_state == MAY_TLS_STREAM_FAILED ||
            tls_state == MAY_TLS_STREAM_CLOSED || ticks - started >= DEADLINE) {
            stop_transfer();
            finish(TRY_AGAIN);
        } else if (length && received == length) {
            may_tls_stream_close();
            phase = DRAIN;
            started = ticks;
        }
        break;
    }
    case DRAIN: {
        may_tls_stream_poll();
        int tls_state = may_tls_stream_state();
        if (tls_state == MAY_TLS_STREAM_READY && ticks - started < 300u) break;
        if (tls_state == MAY_TLS_STREAM_READY) may_tls_stream_abort();
        if (!want_image) {
            if (!read_metadata()) finish(TRY_AGAIN);
            else if (date[0] && strcmp(candidate, date) <= 0) finish(CHECK_AGAIN);
            else {
                want_image = 1;
                phase = CONNECT;
                started = ticks;
            }
        } else if (may_bing_decode_jpeg(content, received, pixels)) {
            memcpy(date, candidate, sizeof candidate);
            serial("MayOS: native Bing daily wallpaper ready\n");
            finish(CHECK_AGAIN);
            return 1;
        } else {
            finish(TRY_AGAIN);
        }
        break;
    }
    }
    return 0;
}

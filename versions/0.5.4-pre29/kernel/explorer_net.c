#include "core.h"
#include "net.h"
#include "net_stream_io.h"
#include "security_trusted_time.h"
#ifdef MAYOS_USE_BEARSSL
#define memcpy maybear_memcpy
#define memset maybear_memset
#define strlen maybear_strlen
#include "security_tls_stream.h"
#undef memcpy
#undef memset
#undef strlen
#endif

enum WebPhase { WEB_IDLE, WEB_DNS, WEB_CONNECT, WEB_RECEIVE };
static enum WebPhase phase;
static int secure;
static u16 port;
static u32 began, sent, received;
static u8 address[4];
static char host[128], request[768], wire[12288], result[4096];

static int prefix(const char *s, const char *literal) {
    while (*literal)
        if (*s++ != *literal++) return 0;
    return 1;
}

static int alphabet(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static int parse_ipv4(const char *name) {
    const char *p = name;
    for (int i = 0; i < 4; i++) {
        unsigned n = 0;
        const char *first = p;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + *p++ - '0';
            if (n > 255) return 0;
        }
        if (p == first || (i < 3 ? *p != '.' : *p != 0)) return 0;
        address[i] = (u8)n;
        if (i < 3) p++;
    }
    return 1;
}

static int make_request(const char *url) {
    secure = prefix(url, "https://");
    if (!secure && !prefix(url, "http://")) return 0;
    const char *route = url + (secure ? 8 : 7), *start = route;
    while (alphabet(*route) || *route == '.' || *route == '-') route++;
    if (route == start || route - start >= (int)sizeof(host)) return 0;
    u32 name_size = (u32)(route - start);
    memcpy(host, start, name_size);
    host[name_size] = 0;
    port = secure ? 443 : 80;
    if (*route == ':') {
        u32 value = 0;
        const char *digits = ++route;
        while (*route >= '0' && *route <= '9') {
            value = value * 10 + *route++ - '0';
            if (value > 65535) return 0;
        }
        if (!value || route == digits) return 0;
        port = (u16)value;
    }
    u32 authority = (u32)(route - start);
    if (*route && *route != '/' && *route != '?') return 0;
    for (const char *p = route; *p; p++)
        if (*p <= ' ' || *p >= 127) return 0;
    if (strlen(route) + authority >= 600) return 0;
    copy(request, "GET ", sizeof(request));
    if (*route != '/') copy(request + 4, "/", sizeof(request) - 4);
    u32 offset = strlen(request);
    copy(request + offset, route, sizeof(request) - offset);
    offset = strlen(request);
    copy(request + offset, " HTTP/1.1\r\nHost: ", sizeof(request) - offset);
    offset = strlen(request);
    memcpy(request + offset, start, authority);
    offset += authority;
    copy(request + offset, "\r\nUser-Agent: MayOS-Explorer/1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
         sizeof(request) - offset);
    return 1;
}

static void end_request(const char *message) {
    if (phase == WEB_RECEIVE) {
#ifdef MAYOS_USE_BEARSSL
        if (secure) may_tls_stream_abort();
        else
#endif
        may_stream_abort();
    }
    phase = WEB_IDLE;
    if (message) copy(result, message, sizeof(result));
}

void platform_web_cancel(void) {
    end_request("Request cancelled.");
}

int platform_web_start(const char *url) {
    if (phase != WEB_IDLE) end_request(0);
    if (!make_request(url)) {
        end_request("Enter a valid HTTP or HTTPS address.");
        return 0;
    }
#ifdef MAYOS_USE_BEARSSL
    unsigned days, seconds;
    if (secure && !may_time_read(&days, &seconds)) {
        end_request("HTTPS waiting for authenticated UTC. Check Date & Time.");
        return 0;
    }
#else
    if (secure) {
        end_request("HTTPS is unavailable in this build.");
        return 0;
    }
#endif
    if (may_net_dhcp_info()->state != MAY_DHCP_BOUND) {
        end_request("Network is not ready. Wait for a DHCP lease.");
        return 0;
    }
    int numeric = parse_ipv4(host);
    if (!numeric && !may_net_dns_lookup(host)) {
        end_request("DNS is busy or unavailable. Try again shortly.");
        return 0;
    }
    began = ticks;
    phase = numeric ? WEB_CONNECT : WEB_DNS;
    sent = received = 0;
    wire[0] = 0;
    copy(result, "Resolving host…", sizeof(result));
    return 1;
}

static int delimiter(const char *s, const char *end) {
    return end - s >= 4 && s[0] == '\r' && s[1] == '\n' && s[2] == '\r' && s[3] == '\n';
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; }

static int header_prefix(const char *p, const char *end, const char *name) {
    while (*name) {
        if (p == end || lower(*p++) != *name++) return 0;
    }
    return 1;
}

/* A complete length-delimited body is authenticated by TLS records even
 * when a server closes TCP without sending the optional close alert. Never
 * treat a prefix, conflicting length, or Transfer-Encoding as complete here. */
static u32 complete_body(void) {
    const char *end = wire + received, *headers = wire;
    while (end - headers >= 4 && !delimiter(headers, end)) headers++;
    if (end - headers < 4) return 0;
    int found = 0;
    u32 length = 0;
    for (const char *line = wire; line < headers; line++) {
        if (*line != '\n') continue;
        const char *field = line + 1;
        if (header_prefix(field, headers, "transfer-encoding:")) return 0;
        if (!header_prefix(field, headers, "content-length:")) continue;
        if (found) return 0;
        field += 15;
        while (field < headers && (*field == ' ' || *field == '\t')) field++;
        if (field >= headers || *field < '0' || *field > '9') return 0;
        while (field < headers && *field >= '0' && *field <= '9') {
            length = length * 10u + (u32)(*field++ - '0');
            if (length > sizeof(wire)) return 0;
        }
        while (field < headers && (*field == ' ' || *field == '\t')) field++;
        if (field < headers && *field != '\r') return 0;
        found = 1;
    }
    if (!found || (u32)(end - (headers + 4)) < length) return 0;
    return (u32)(headers + 4 - wire) + length;
}

static int tag_is(const char *name, const char *end, const char *wanted) {
    if (!header_prefix(name, end, wanted)) return 0;
    const char *after = name + strlen(wanted);
    return after == end || *after <= ' ' || *after == '/';
}

static void copy_preview(const char *body, const char *end, int html) {
    static const char *hidden_tags[] = {"head", "script", "style"};
    static const char *break_tags[] = {"p", "br", "div", "li", "h1", "h2", "h3", "tr"};
    static const struct { const char *name; char value; } entities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
        {"&quot;", '"'}, {"&#39;", 39}, {"&nbsp;", ' '}
    };
    int hidden = -1;
    u32 size = 0;
    while (body < end && size + 1 < sizeof(result)) {
        if (html && *body == '<' && body + 1 < end &&
            (alphabet(body[1]) || body[1] == '/' || body[1] == '!')) {
            const char *tag = ++body;
            const char *close = tag;
            if (end - tag >= 3 && tag[0] == '!' && tag[1] == '-' && tag[2] == '-') {
                while (end - close >= 3 && !(close[0] == '-' && close[1] == '-' && close[2] == '>')) close++;
                if (end - close < 3) break;
                body = close + 3;
                continue;
            }
            char quote = 0;
            while (close < end) {
                char c = *close;
                if (quote) {
                    if (c == quote) quote = 0;
                } else if (c == '>') break;
                else if (c == '"' || c == 39) quote = c;
                close++;
            }
            if (close == end) break;
            body = close + 1;
            if (hidden >= 0) {
                if (*tag == '/' && tag_is(tag + 1, close, hidden_tags[hidden])) hidden = -1;
                continue;
            }
            for (unsigned i = 0; i < sizeof(hidden_tags) / sizeof(hidden_tags[0]); i++)
                if (tag_is(tag, close, hidden_tags[i])) hidden = (int)i;
            if (*tag == '/') tag++;
            for (unsigned i = 0; i < sizeof(break_tags) / sizeof(break_tags[0]); i++)
                if (tag_is(tag, close, break_tags[i]) && size && result[size - 1] != '\n')
                    result[size++] = '\n';
            continue;
        }
        unsigned char c = *body++;
        if (hidden >= 0) continue;
        if (html && c == '&') {
            for (unsigned i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
                if (!header_prefix(body - 1, end, entities[i].name)) continue;
                body += strlen(entities[i].name) - 1;
                c = (unsigned char)entities[i].value;
                break;
            }
        }
        if (html && c <= ' ') {
            if (size && result[size - 1] != ' ' && result[size - 1] != '\n') result[size++] = ' ';
        } else if (c >= 32 || c == '\n' || c == '\t') result[size++] = (char)c;
    }
    while (html && size && (result[size - 1] == ' ' || result[size - 1] == '\n')) size--;
    result[size] = 0;
    if (!size) copy(result, "Response contains no displayable text.", sizeof(result));
}

static void finish_response(void) {
    const char *header = wire, *end = wire + received;
    while (end - header >= 4 && !delimiter(header, end)) header++;
    if (end - header < 4 || !prefix(wire, "HTTP/1.")) {
        end_request("Incomplete HTTP response.");
        return;
    }
    if (received < 12 || wire[8] != ' ' || wire[9] != '2' ||
        wire[10] < '0' || wire[10] > '9' || wire[11] < '0' || wire[11] > '9') {
        char status[16];
        memcpy(status, wire, received < 12 ? received : 12);
        status[received < 12 ? received : 12] = 0;
        end_request(status);
        return;
    }
    int chunked = 0, html = 0;
    for (const char *line = wire; line < header; line++) {
        if (*line == '\n' && header_prefix(line + 1, header, "content-type:")) {
            const char *type = line + 14;
            while (type < header && (*type == ' ' || *type == '\t')) type++;
            html = header_prefix(type, header, "text/html");
        }
        if (*line == '\n' && header_prefix(line + 1, header, "transfer-encoding:")) {
            const char *encoding = line + 19;
            while (encoding < header && (*encoding == ' ' || *encoding == '\t')) encoding++;
            chunked = header_prefix(encoding, header, "chunked");
            if (!chunked) {
                end_request("Unsupported transfer encoding.");
                return;
            }
        }
    }
    const char *body = header + 4;
    if (chunked) {
        char *output = wire;
        const char *p = body;
        while (p < end) {
            u32 bytes = 0, digits = 0;
            while (p < end) {
                char c = lower(*p);
                if (c >= '0' && c <= '9') bytes = bytes * 16 + c - '0';
                else if (c >= 'a' && c <= 'f') bytes = bytes * 16 + c - 'a' + 10;
                else break;
                if (++digits > 6 || bytes > sizeof(wire)) break;
                p++;
            }
            if (!digits || end - p < 2 || p[0] != '\r' || p[1] != '\n') break;
            p += 2;
            if (!bytes) break;
            u32 available = (u32)(end - p);
            u32 count = bytes < available ? bytes : available;
            for (u32 i = 0; i < count; i++) *output++ = *p++;
            if (end - p < 2 || p[0] != '\r' || p[1] != '\n') break;
            p += 2;
        }
        body = wire;
        end = output;
    }
    copy_preview(body, end, html);
    end_request(0);
    serial("MayOS: Explorer received HTTP response\n");
}

int platform_web_poll(char *destination, int capacity) {
    if (phase != WEB_IDLE && ticks - began >= 3000u) end_request("Request timed out.");
    if (phase == WEB_DNS) {
        int found = may_net_dns_result(host, address);
        if (found < 0) end_request("Could not resolve host.");
        if (found > 0) phase = WEB_CONNECT;
    }
    if (phase == WEB_CONNECT) {
        int opened;
#ifdef MAYOS_USE_BEARSSL
        if (secure) opened = may_tls_public_open(address, port, host);
        else
#endif
        opened = may_stream_open(address, port);
        if (opened) {
            phase = WEB_RECEIVE;
            copy(result, "Loading…", sizeof(result));
        }
    }
    if (phase == WEB_RECEIVE) {
        u32 total = strlen(request), available = sizeof(wire) - 1 - received, read = 0;
        int closed, failed;
#ifdef MAYOS_USE_BEARSSL
        if (secure) {
            may_tls_stream_poll();
            if (sent < total) sent += may_tls_stream_write((const u8 *)request + sent, total - sent);
            read = may_tls_stream_read((u8 *)wire + received, available);
            int state = may_tls_stream_state();
            closed = state == MAY_TLS_STREAM_CLOSED;
            failed = state == MAY_TLS_STREAM_FAILED;
        } else
#endif
        {
            if (sent < total) sent += may_stream_write((const u8 *)request + sent, total - sent);
            read = may_stream_read((u8 *)wire + received, available);
            int state = may_stream_status();
            closed = state == MAY_STREAM_EOF || state == MAY_STREAM_CLOSED;
            failed = state == MAY_STREAM_FAILED;
        }
        received += read;
        wire[received] = 0;
        u32 complete = read ? complete_body() : 0;
        if (complete) {
            received = complete;
            wire[received] = 0;
            finish_response();
        }
        else if (failed) end_request(secure ? "HTTPS verification or connection failed." : "HTTP connection failed.");
        else if ((closed && !read) || received == sizeof(wire) - 1) finish_response();
    }
    if (capacity > 0) copy(destination, result, (u32)capacity);
    return phase != WEB_IDLE;
}

#include "updater.h"
#include "version.h"
#include "update_catalog.h"
#include "update_sha256.h"

static MayUpdaterState state;
static MayUpdateSha256 stream_hash;
static int stream_active;

static unsigned int slen(const char *s) {
    unsigned int n = 0;
    if (!s) return 0;
    while (s[n] && n < 4096u) n++;
    return n;
}
static int same(const char *a, const char *b) {
    unsigned int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == b[i];
}
static int prefix(const char *s, const char *pre) {
    for (unsigned int i = 0; pre[i]; i++)
        if (s[i] != pre[i]) return 0;
    return 1;
}
static int copy_to(char *dst, unsigned int cap, const char *src) {
    unsigned int n = slen(src);
    if (n >= cap) return 0;
    for (unsigned int i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
    return 1;
}
static void clear(void *p, unsigned int n) {
    unsigned char *b = p;
    for (unsigned int i = 0; i < n; i++) b[i] = 0;
}
static int decimal(const char *s, unsigned int *out, unsigned int limit) {
    unsigned int v = 0;
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        unsigned int d = (unsigned int)(*s++ - '0');
        if (v > (limit - d) / 10u) return 0;
        v = v * 10u + d;
    }
    *out = v;
    return 1;
}
/* Only use the numeric major.minor.patch and standardized preN/dev suffix.
 * Unknown qualifiers are not assumed to be safe upgrades. */
static int read_version(const char *input, unsigned int parts[4]) {
    const char *s = *input == 'v' ? input + 1 : input;
    for (int i = 0; i < 3; i++) {
        unsigned int value = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (value > 100000u) return 0;
            value = value * 10u + (unsigned int)(*s++ - '0');
            digits++;
        }
        if (!digits) return 0;
        parts[i] = value;
        if (i < 2) {
            if (*s++ != '.') return 0;
        }
    }
    if (!*s) { parts[3] = 1000000u; return 1; } /* stable > prerelease */
    if (*s++ != '-') return 0;
    if (prefix(s, "pre")) {
        s += 3;
        unsigned int n = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (n > 9999u) return 0;
            n = n * 10u + (unsigned int)(*s++ - '0');
            digits++;
        }
        if (!digits) return 0;
        if (*s && !same(s, "-dev")) return 0;
        parts[3] = 100u + n * 2u + (*s ? 0u : 1u);
        return 1;
    }
    if (same(s, "dev")) { parts[3] = 1u; return 1; }
    return 0;
}
int may_updater_compare(const char *a, const char *b) {
    unsigned int va[4], vb[4];
    if (!read_version(a, va) || !read_version(b, vb)) return 0;
    for (int i = 0; i < 4; ++i) {
        if (va[i] > vb[i]) return 1;
        if (va[i] < vb[i]) return -1;
    }
    return 0;
}
const char *may_updater_current_version(void) { return MAYOS_VERSION; }
const MayUpdaterState *may_updater_state(void) { return &state; }

static int valid_sha(const char *s) {
    if (slen(s) != 64u) return 0;
    for (int i = 0; i < 64; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'f') ||
              (s[i] >= '0' && s[i] <= '9'))) return 0;
    return 1;
}
static int valid_url(const char *s, const char *tag) {
    const char *base = "https://github.com/ZhongYeMay/MayOS/releases/download/";
    if (!prefix(s, base)) return 0;
    s += slen(base);
    for (unsigned int i = 0; tag[i]; ++i)
        if (*s++ != tag[i]) return 0;
    if (*s++ != '/') return 0;
    if (!prefix(s, "MayOS-")) return 0;
    unsigned int n = slen(s);
    if (n < 8 || !same(s + n - 4, ".iso")) return 0;
    for (unsigned int i = 0; i < n; ++i)
        if (s[i] == '?' || s[i] == '#' || s[i] == '@' ||
            s[i] == '\\' || s[i] == ' ') return 0;
    return 1;
}
static int offer(const MayUpdateRelease *r) {
#ifdef MAYOS_LINUX
    if (!same(r->variant, "dual")) return 0;
#else
    if (!same(r->variant, "maykernel")) return 0;
#endif
    if (!same(r->arch, "x86_64") || !valid_sha(r->sha256) ||
        !valid_url(r->url, r->tag) ||
        r->bytes < 1024u * 1024u || r->bytes > 512u * 1024u * 1024u)
        return 0;
    unsigned int parts[4];
    if (!read_version(r->tag, parts)) return 0;
    if (!state.has_latest || may_updater_compare(r->tag, state.latest.tag) > 0) {
        state.latest = *r;
        state.has_latest = 1;
        state.has_upgrade = may_updater_compare(r->tag, MAYOS_VERSION) > 0;
        if (state.phase == MAY_UPDATE_AVAILABLE || state.phase == MAY_UPDATE_NO_NEWER)
            state.phase = state.has_upgrade ? MAY_UPDATE_AVAILABLE : MAY_UPDATE_NO_NEWER;
    }
    return 1;
}
void may_updater_init(void) {
    clear(&state, (unsigned int)sizeof(state));
    stream_active = 0;
    state.phase = MAY_UPDATE_OFFLINE;
    copy_to(state.message, sizeof(state.message), "Saved verified GitHub Release metadata");
    for (unsigned int i = 0; i < MAYOS_UPDATE_CATALOG_COUNT; ++i) {
        const MayUpdateCatalogRow *row = &mayos_update_catalog[i];
        MayUpdateRelease entry;
        clear(&entry, (unsigned int)sizeof(entry));
        if (!copy_to(entry.tag, sizeof(entry.tag), row->tag) ||
            !copy_to(entry.variant, sizeof(entry.variant), row->variant) ||
            !copy_to(entry.name, sizeof(entry.name), row->name) ||
            !copy_to(entry.kernel, sizeof(entry.kernel), row->kernel) ||
            !copy_to(entry.date, sizeof(entry.date), row->date) ||
            !copy_to(entry.arch, sizeof(entry.arch), row->arch) ||
            !copy_to(entry.sha256, sizeof(entry.sha256), row->sha256) ||
            !copy_to(entry.url, sizeof(entry.url), row->url) ||
            !copy_to(entry.notes, sizeof(entry.notes), row->notes))
            continue;
        entry.bytes = row->bytes;
        entry.preview = 1;
        offer(&entry);
    }
}
void may_updater_offline(void) {
    state.online_checked = 0;
    state.phase = MAY_UPDATE_OFFLINE;
    copy_to(state.message, sizeof(state.message),
            "GitHub live check unavailable; cached release metadata");
}
/* A checked HTTPS transport may pass a single trusted feed line here.
 * Validate ALL fields and reject malformed or off-domain download links.
 * Network transport itself is never implemented in this parser. */
int may_updater_feed_line(const char *line) {
    char scratch[660], *fields[10];
    unsigned int len = slen(line);
    if (len == 0 || len >= sizeof(scratch)) return 0;
    if (line[0] == '#' || prefix(line, "MAYOS_UPDATE_FEED_V1")) return 0;
    for (unsigned int i = 0; i <= len; ++i) scratch[i] = line[i];
    int n = 1;
    fields[0] = scratch;
    for (unsigned int i = 0; i < len; ++i) {
        if (scratch[i] == '\n' || scratch[i] == '\r') return 0;
        if (scratch[i] == '|') {
            if (n >= 10) return 0;
            scratch[i] = 0;
            fields[n++] = scratch + i + 1;
        }
    }
    if (n != 10) return 0;
    MayUpdateRelease e;
    clear(&e, (unsigned int)sizeof(e));
    if (!copy_to(e.tag, sizeof(e.tag), fields[0]) ||
        !copy_to(e.variant, sizeof(e.variant), fields[1]) ||
        !copy_to(e.name, sizeof(e.name), fields[2]) ||
        !copy_to(e.sha256, sizeof(e.sha256), fields[4]) ||
        !copy_to(e.date, sizeof(e.date), fields[5]) ||
        !copy_to(e.arch, sizeof(e.arch), fields[6]) ||
        !copy_to(e.kernel, sizeof(e.kernel), fields[7]) ||
        !copy_to(e.url, sizeof(e.url), fields[8]) ||
        !copy_to(e.notes, sizeof(e.notes), fields[9])) return 0;
    if (!decimal(fields[3], &e.bytes, 512u * 1024u * 1024u)) return 0;
    e.preview = 1;
    return offer(&e);
}
void may_updater_checking(void) {
    state.phase = MAY_UPDATE_CHECKING;
    state.online_checked = 0;
    copy_to(state.message, sizeof(state.message), "Checking GitHub release feed via TLS");
}
void may_updater_checked(void) {
    state.online_checked = 1;
    state.phase = state.has_upgrade ? MAY_UPDATE_AVAILABLE : MAY_UPDATE_NO_NEWER;
    copy_to(state.message, sizeof(state.message),
            state.has_upgrade ? "New GitHub Release found" : "No newer compatible GitHub Release");
}
/* Trust only verified transport and real bytes, not a UI timer or estimate. */
int may_updater_stream_begin(void) {
    if (!state.online_checked || !state.has_upgrade || !state.has_latest ||
        state.phase != MAY_UPDATE_AVAILABLE || !valid_sha(state.latest.sha256) ||
        !valid_url(state.latest.url, state.latest.tag))
        return 0;
    may_update_sha256_init(&stream_hash);
    state.transferred = 0;
    state.total = state.latest.bytes;
    state.elapsed_seconds = 0;
    state.verified = 0;
    stream_active = 1;
    state.phase = MAY_UPDATE_DOWNLOADING;
    copy_to(state.message, sizeof(state.message), "Receiving TLS-authenticated bytes");
    return 1;
}
int may_updater_stream_chunk(const void *bytes, unsigned int size,
                             unsigned int elapsed_seconds) {
    if (!stream_active || state.phase != MAY_UPDATE_DOWNLOADING) return 0;
    if (!bytes && size) {
        may_updater_failed("Missing download data");
        return 0;
    }
    if ((unsigned long long)size > state.total - state.transferred) {
        may_updater_failed("Downloaded more bytes than manifest permits");
        return 0;
    }
    may_update_sha256_write(&stream_hash, bytes, size);
    state.transferred += size;
    state.elapsed_seconds = elapsed_seconds;
    return 1;
}
int may_updater_stream_finish(void) {
    if (!stream_active || state.phase != MAY_UPDATE_DOWNLOADING) return 0;
    stream_active = 0;
    if (state.transferred != state.total) {
        may_updater_failed("Truncated download: byte count mismatch");
        return 0;
    }
    state.phase = MAY_UPDATE_VERIFYING;
    copy_to(state.message, sizeof(state.message), "Computing SHA-256 of received content");
    char digest[65];
    may_update_sha256_hex(&stream_hash, digest);
    if (!same(digest, state.latest.sha256)) {
        may_updater_failed("SHA-256 mismatch: refusing update");
        return 0;
    }
    state.verified = 1;
    state.phase = MAY_UPDATE_VERIFIED;
    copy_to(state.message, sizeof(state.message),
            "SHA-256 verified; boot installer and rollback unavailable");
    return 1;
}
void may_updater_failed(const char *message) {
    stream_active = 0;
    state.phase = MAY_UPDATE_FAILED;
    state.verified = 0;
    copy_to(state.message, sizeof(state.message), message ? message : "Update failed");
}
unsigned int may_updater_download_percent(void) {
    if (!state.total) return 0;
    return (unsigned int)(state.transferred * 100u / state.total);
}
unsigned int may_updater_download_eta(void) {
    if (!state.elapsed_seconds || !state.transferred ||
        state.transferred >= state.total) return 0;
    unsigned long long seconds =
        (state.total - state.transferred) * state.elapsed_seconds / state.transferred;
    if (seconds > 86400ull) return 86400u;
    return (unsigned int)seconds;
}

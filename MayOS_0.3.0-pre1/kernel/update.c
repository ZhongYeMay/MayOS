#include "update.h"
#include "version.h"

/* This parser deliberately has no network or block-device write dependency.
 * All input is bounded by File.size, not a trusted NUL terminator. */
static MayUpdateInfo update;

static int printable_copy(char *dst, u32 cap, const char *value) {
    u32 n = 0;
    if (!value[0]) return 0;
    while (value[n]) {
        u8 c = (u8)value[n];
        if (c < 32 || c > 126 || n + 1 >= cap) return 0;
        dst[n] = (char)c;
        n++;
    }
    dst[n] = 0;
    return 1;
}

static int decimal(const char *s, u32 *out) {
    u32 value = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        u32 digit = (u32)(*s - '0');
        if (value > (0xffffffffu - digit) / 10u) return 0;
        value = value * 10u + digit;
    }
    *out = value;
    return 1;
}

static int date_valid(const char *s) {
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return 0;
    int year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 +
               (s[2] - '0') * 10 + (s[3] - '0');
    int month = (s[5] - '0') * 10 + (s[6] - '0');
    int day = (s[8] - '0') * 10 + (s[9] - '0');
    return year >= 2023 && month >= 1 && month <= 12 &&
           day >= 1 && day <= 31;
}

static int hex256_valid(const char *s) {
    if (strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

typedef struct {
    u32 major, minor, patch, pre;
    int is_final, is_dev;
} ParsedVersion;

static int next_number(const char **cursor, u32 *out) {
    const char *start = *cursor;
    u32 v = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        u32 d = (u32)(**cursor - '0');
        if (v > (1000000u - d) / 10u) return 0;
        v = v * 10 + d;
        (*cursor)++;
    }
    if (*cursor == start) return 0;
    *out = v;
    return 1;
}

static int version_parse(const char *s, ParsedVersion *v) {
    memset(v, 0, sizeof(*v));
    if (!next_number(&s, &v->major) || *s++ != '.' ||
        !next_number(&s, &v->minor) || *s++ != '.' ||
        !next_number(&s, &v->patch)) return 0;
    if (*s == 0) {
        v->is_final = 1;
        return 1;
    }
    if (s[0] != '-' || s[1] != 'p' || s[2] != 'r' || s[3] != 'e')
        return 0;
    s += 4;
    if (!next_number(&s, &v->pre)) return 0;
    if (*s == 0) return 1;
    if (s[0] == '-' && s[1] == 'd' && s[2] == 'e' &&
        s[3] == 'v' && s[4] == 0) {
        v->is_dev = 1;
        return 1;
    }
    return 0;
}

static int compare_versions(const char *candidate, const char *current, int *result) {
    ParsedVersion a, b;
    if (!version_parse(candidate, &a) || !version_parse(current, &b))
        return 0;
#define CMP(field) do { if (a.field != b.field) { \
    *result = a.field > b.field ? 1 : -1; return 1; } } while (0)
    CMP(major);
    CMP(minor);
    CMP(patch);
    CMP(is_final);
    if (!a.is_final) {
        CMP(pre);
        /* pre2-dev < pre2 < pre3 < final */
        if (a.is_dev != b.is_dev) {
            *result = a.is_dev ? -1 : 1;
            return 1;
        }
    }
#undef CMP
    *result = 0;
    return 1;
}

static int parse_pair(char *line, u32 *seen) {
    char *eq = line;
    while (*eq && *eq != '=') eq++;
    if (!*eq || eq == line || !eq[1]) return 0;
    *eq++ = 0;
    u32 mask = 0;
    int ok = 0;
    if (!strcmp(line, "name")) {
        mask = 1u << 0;
        ok = printable_copy(update.name, sizeof(update.name), eq);
    } else if (!strcmp(line, "version")) {
        mask = 1u << 1;
        ok = printable_copy(update.version, sizeof(update.version), eq);
    } else if (!strcmp(line, "architecture")) {
        mask = 1u << 2;
        ok = printable_copy(update.architecture, sizeof(update.architecture), eq);
    } else if (!strcmp(line, "kernel")) {
        mask = 1u << 3;
        ok = printable_copy(update.kernel, sizeof(update.kernel), eq);
    } else if (!strcmp(line, "release_date")) {
        mask = 1u << 4;
        ok = printable_copy(update.release_date, sizeof(update.release_date), eq);
    } else if (!strcmp(line, "size_bytes")) {
        mask = 1u << 5;
        ok = decimal(eq, &update.size_bytes) && update.size_bytes > 0;
    } else if (!strcmp(line, "sha256")) {
        mask = 1u << 6;
        ok = printable_copy(update.sha256, sizeof(update.sha256), eq);
    } else if (!strcmp(line, "eta_minutes")) {
        mask = 1u << 7;
        ok = decimal(eq, &update.eta_minutes) &&
             update.eta_minutes >= 1 && update.eta_minutes <= 1440;
    } else if (!strcmp(line, "changelog")) {
        mask = 1u << 8;
        ok = printable_copy(update.changelog, sizeof(update.changelog), eq);
    } else {
        return 0;
    }
    if (!ok || (*seen & mask)) return 0;
    *seen |= mask;
    return 1;
}

void may_update_check(void) {
    memset(&update, 0, sizeof(update));
    int index = fs_find(1, "Update.may");
    if (index < 0) {
        update.state = MAY_UPDATE_MISSING;
        return;
    }
    update.state = MAY_UPDATE_INVALID;
    if (!files[index].used || files[index].dir ||
        files[index].size == 0 || files[index].size > 2048u)
        return;
    const u8 *data = files[index].data;
    u32 seen = 0, offset = 0;
    while (offset < files[index].size) {
        char line[160];
        u32 n = 0;
        while (offset < files[index].size && data[offset] != '\n') {
            u8 ch = data[offset++];
            if (ch == 0 || n + 1 >= sizeof(line)) return;
            line[n++] = (char)ch;
        }
        if (offset < files[index].size) offset++;
        if (n && line[n - 1] == '\r') n--;
        line[n] = 0;
        if (!n || line[0] == '#') continue;
        if (!parse_pair(line, &seen)) return;
    }
    if (seen != 0x1ffu ||
        strcmp(update.architecture, "x86_64") ||
        strcmp(update.kernel, "MayKernel") ||
        !date_valid(update.release_date) ||
        !hex256_valid(update.sha256)) return;
    int comparison = 0;
    if (!compare_versions(update.version, MAYOS_VERSION, &comparison)) return;
    update.state = comparison > 0 ? MAY_UPDATE_CANDIDATE : MAY_UPDATE_CURRENT;
}

const MayUpdateInfo *may_update_info(void) {
    return &update;
}

const char *may_update_status(void) {
    switch (update.state) {
        case MAY_UPDATE_MISSING: return "No local manifest in Documents/Update.may";
        case MAY_UPDATE_INVALID: return "Manifest invalid or incompatible (no changes made)";
        case MAY_UPDATE_CURRENT: return "No newer version in local manifest";
        case MAY_UPDATE_CANDIDATE: return "Newer version found (UNVERIFIED metadata)";
        default: return "Not checked - local only; GitHub unavailable";
    }
}

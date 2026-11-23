#ifndef MAYOS_UTF8_H
#define MAYOS_UTF8_H

/* Malformed UTF-8 consumes one byte. A caller may render the replacement
 * character without modifying the underlying MayFS file. */
static inline unsigned may_utf8_next(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    if (!p[0]) return 0;
    unsigned cp = p[0];
    int count = 1;
    if (cp >= 0xc2 && cp <= 0xdf && (p[1] & 0xc0) == 0x80) {
        cp = (cp & 31) << 6 | (p[1] & 63);
        count = 2;
    } else if (cp >= 0xe0 && cp <= 0xef && (p[1] & 0xc0) == 0x80 &&
               (p[2] & 0xc0) == 0x80) {
        unsigned value = (cp & 15) << 12 | (p[1] & 63) << 6 | (p[2] & 63);
        if (value >= 0x800 && (value < 0xd800 || value > 0xdfff)) {
            cp = value;
            count = 3;
        }
    } else if (cp >= 0xf0 && cp <= 0xf4 && (p[1] & 0xc0) == 0x80 &&
               (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        unsigned value = (cp & 7) << 18 | (p[1] & 63) << 12 | (p[2] & 63) << 6 | (p[3] & 63);
        if (value >= 0x10000 && value <= 0x10ffff) {
            cp = value;
            count = 4;
        }
    }
    *cursor += count;
    return count == 1 && cp >= 0x80 ? 0xfffd : cp;
}

static inline unsigned may_utf8_put(char *out, unsigned cp) {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp == 0) return 0;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    unsigned count = cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    for (unsigned i = count - 1; i > 0; --i) {
        out[i] = (char)(0x80 | (cp & 63));
        cp >>= 6;
    }
    static const unsigned char prefix[] = {0, 0, 0xc0, 0xe0, 0xf0};
    out[0] = (char)(prefix[count] | cp);
    return count;
}

static inline int may_utf8_extension(unsigned cp) {
    return (cp >= 0x300 && cp <= 0x36f) || cp == 0x3099 || cp == 0x309a ||
           (cp >= 0x1ab0 && cp <= 0x1aff) || (cp >= 0x1dc0 && cp <= 0x1dff) ||
           (cp >= 0x20d0 && cp <= 0x20ff) || (cp >= 0xfe00 && cp <= 0xfe0f) ||
           (cp >= 0xfe20 && cp <= 0xfe2f) || (cp >= 0xe0100 && cp <= 0xe01ef) ||
           (cp >= 0x1f3fb && cp <= 0x1f3ff);
}

static inline int may_utf8_regional(unsigned cp) {
    return cp >= 0x1f1e6 && cp <= 0x1f1ff;
}

static inline void may_utf8_backspace(char *value) {
    const char *cursor = value;
    unsigned last = 0, flags = 0;
    char *cluster = value;
    while (*cursor) {
        const char *start = cursor;
        unsigned cp = may_utf8_next(&cursor);
        if (!may_utf8_extension(cp) && cp != 0x200d && last != 0x200d &&
            !(last == '\r' && cp == '\n') && !(may_utf8_regional(cp) && flags % 2))
            cluster = (char *)start;
        flags = may_utf8_regional(cp) ? flags + 1 : 0;
        last = cp;
    }
    *cluster = 0;
}

#endif

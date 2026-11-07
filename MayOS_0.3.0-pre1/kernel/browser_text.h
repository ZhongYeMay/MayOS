/* Shared, bounded, script-free HTTP/HTML text preview for both kernels.
 * Material desktop Explorer is not an HTML browser or TLS client. */
#ifndef MAYOS_BROWSER_TEXT_H
#define MAYOS_BROWSER_TEXT_H

static int browse_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static char browse_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}
static int browse_tag(const char *text, const char *name) {
    while (*name) {
        if (browse_lower(*text++) != *name++) return 0;
    }
    return !browse_word_char(*text);
}
static void browse_add(char *dest, int limit, int *used, char c) {
    if (limit < 2) return;
    if (c == '\r') return;
    if (c == '\t') c = ' ';
    if (c != '\n' && (unsigned char)c < 32) return;
    if (c == '\n' && (!*used || dest[*used - 1] == '\n')) return;
    if (c == ' ' && *used && dest[*used - 1] == ' ') return;
    if (*used < limit - 1) {
        dest[(*used)++] = c;
        dest[*used] = '\0';
    }
}
static void browse_newline(char *dest, int limit, int *used) {
    if (*used && dest[*used - 1] == ' ') (*used)--;
    if (*used < limit - 1) browse_add(dest, limit, used, '\n');
}
static int browse_entity(const char *s, char *decoded) {
    const char *name[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&apos;", "&nbsp;"};
    const char values[] = {'&', '<', '>', '"', '\'', ' '};
    for (int i = 0; i < 6; i++) {
        int j = 0;
        while (name[i][j] && s[j] == name[i][j]) j++;
        if (!name[i][j]) { *decoded = values[i]; return j; }
    }
    return 0;
}
static void browse_preview(const char *raw, char *dest, int capacity) {
    if (!dest || capacity < 2) return;
    dest[0] = '\0';
    if (!raw) return;
    int used = 0;
    const char *body = raw;
    int html = 0;
    if (raw[0] == 'H' && raw[1] == 'T' && raw[2] == 'T' &&
        raw[3] == 'P' && raw[4] == '/') {
        /* Display HTTP status without dumping raw response headers. */
        const char *p = raw;
        while (*p && *p != ' ' && *p != '\n') p++;
        if (*p == ' ') {
            browse_add(dest, capacity, &used, 'H');
            browse_add(dest, capacity, &used, 'T');
            browse_add(dest, capacity, &used, 'T');
            browse_add(dest, capacity, &used, 'P');
            browse_add(dest, capacity, &used, ' ');
            for (int i = 1; i <= 3 && p[i]; i++)
                browse_add(dest, capacity, &used, p[i]);
            browse_newline(dest, capacity, &used);
        }
        const char *headers = raw;
        int scanned = 0;
        while (*headers && scanned++ < 512) {
            if (headers[0] == '\r' && headers[1] == '\n' &&
                headers[2] == '\r' && headers[3] == '\n') {
                body = headers + 4; break;
            }
            if (headers[0] == '\n' && headers[1] == '\n') {
                body = headers + 2; break;
            }
            headers++;
        }
        const char *type = raw;
        while (type < body && type < raw + 512 && *type) {
            if (browse_tag(type, "text/html")) { html = 1; break; }
            type++;
        }
    }
    while (*body == ' ' || *body == '\n' || *body == '\r') body++;
    if (*body == '<') html = 1;
    int skip = 0;
    for (const char *p = body; *p && used < capacity - 3; ) {
        if (html && *p == '<') {
            const char *tag = p + 1;
            int closing = *tag == '/';
            if (closing) tag++;
            if (browse_tag(tag, "script") || browse_tag(tag, "style")) {
                if (!closing) skip++;
                else if (skip) skip--;
            }
            if (!skip && (browse_tag(tag, "br") || browse_tag(tag, "p") ||
                browse_tag(tag, "div") || browse_tag(tag, "li") ||
                browse_tag(tag, "h1") || browse_tag(tag, "h2") ||
                browse_tag(tag, "h3") || browse_tag(tag, "/p") ||
                browse_tag(tag, "/div"))) browse_newline(dest, capacity, &used);
            while (*p && *p != '>') p++;
            if (*p) p++;
            continue;
        }
        if (skip) { p++; continue; }
        if (html && *p == '&') {
            char decoded = 0;
            int len = browse_entity(p, &decoded);
            if (len) {
                browse_add(dest, capacity, &used, decoded);
                p += len;
                continue;
            }
        }
        if (*p == '\r') { p++; continue; }
        browse_add(dest, capacity, &used, *p++);
    }
    while (used && (dest[used - 1] == ' ' || dest[used - 1] == '\n'))
        used--;
    dest[used] = '\0';
}

/* Extract safe, clickable HTML anchors for a native links panel. JavaScript,
   file/data URLs and inline handlers are deliberately never interpreted. */
typedef struct {
    char url[176];
    char title[48];
} BrowserLink;

static void browse_piece(char *out, int capacity, const char *first, int count) {
    int i = 0;
    if (capacity < 1) return;
    while (i < capacity - 1 && i < count && first[i]) {
        out[i] = first[i];
        i++;
    }
    out[i] = 0;
}
static int browse_prefix(const char *p, const char *word) {
    for (int i = 0; word[i]; i++) {
        if (p[i] != word[i]) return 0;
    }
    return 1;
}
static int browse_url_safe(const char *value) {
    return browse_prefix(value, "http://") || browse_prefix(value, "https://");
}
static int browse_resolve_link(const char *base, const char *href,
                               char *resolved, int capacity) {
    char candidate[176];
    if (!href[0] || href[0] == '#' || href[0] == '?' || href[0] == '\\')
        return 0;
    if (browse_url_safe(href)) {
        browse_piece(candidate, sizeof(candidate), href, 175);
    } else if (href[0] == '/') {
        if (!browse_url_safe(base) || href[1] == '/') return 0;
        const char *host = base + (base[4] == 's' ? 8 : 7);
        const char *end = host;
        while (*end && *end != '/') end++;
        int length = (int)(end - base);
        if (length + 1 >= (int)sizeof(candidate)) return 0;
        browse_piece(candidate, sizeof(candidate), base, length);
        int i = length;
        for (const char *p = href; *p && i < (int)sizeof(candidate) - 1; p++)
            candidate[i++] = *p;
        candidate[i] = 0;
    } else {
        return 0; /* Relative path without leading slash is not yet supported. */
    }
    if (!browse_url_safe(candidate)) return 0;
    int i = 0;
    for (; candidate[i]; i++) {
        unsigned char c = (unsigned char)candidate[i];
        if (c <= 32 || c > 126 || c == '"' || c == '\'' ||
            c == '<' || c == '>' || i >= capacity - 1) return 0;
    }
    browse_piece(resolved, capacity, candidate, i);
    return 1;
}
static int browse_collect_links(const char *raw, const char *base,
                                BrowserLink *links, int maximum) {
    if (!raw || !base || !links || maximum < 1) return 0;
    if (maximum > 6) maximum = 6;
    int result = 0;
    for (const char *p = raw; *p && result < maximum; p++) {
        if (*p != '<' || browse_lower(p[1]) != 'a' ||
            (p[2] != ' ' && p[2] != '\t')) continue;
        const char *end = p + 2;
        while (*end && *end != '>' && end - p < 900) end++;
        if (*end != '>') continue;
        const char *attr = p + 2;
        char href[176] = "";
        while (attr < end) {
            while (attr < end && (*attr == ' ' || *attr == '\n' || *attr == '\t'))
                attr++;
            if (end - attr >= 6 && browse_lower(attr[0]) == 'h' &&
                browse_lower(attr[1]) == 'r' && browse_lower(attr[2]) == 'e' &&
                browse_lower(attr[3]) == 'f' && attr[4] == '=') {
                const char *val = attr + 5;
                if (val >= end || (*val != '"' && *val != '\'')) break;
                char quote = *val++;
                const char *stop = val;
                while (stop < end && *stop != quote) stop++;
                if (stop < end) browse_piece(href, sizeof(href), val, (int)(stop - val));
                break;
            }
            while (attr < end && *attr != ' ' && *attr != '\n') attr++;
        }
        if (!browse_resolve_link(base, href, links[result].url,
                                 sizeof(links[result].url))) continue;
        const char *label = end + 1, *stop = label;
        while (*stop && *stop != '<' && stop - label < 160) stop++;
        while (label < stop && (*label == ' ' || *label == '\n')) label++;
        if (stop <= label)
            browse_piece(links[result].title, sizeof(links[result].title),
                         links[result].url, 47);
        else browse_piece(links[result].title, sizeof(links[result].title),
                          label, (int)(stop - label));
        result++;
        p = end;
    }
    return result;
}
#endif

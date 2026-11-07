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
#endif

#ifndef MAYOS_RELEASE_FEED_H
#define MAYOS_RELEASE_FEED_H
#include "core.h"

typedef struct {
    char tag[28], name[42], date[11], kernel[32], summary[110], url[224], sha256[65];
    u32 bytes;
} MayReleasePreview;

static int feed_number(const char **p, u32 *value) {
    if (**p < '0' || **p > '9') return 0;
    *value = 0;
    while (**p >= '0' && **p <= '9') {
        u32 digit = (u32)(*(*p)++ - '0');
        if (*value > (0xffffffffu - digit) / 10u) return 0;
        *value = *value * 10u + digit;
    }
    return 1;
}

static int feed_version(const char *p, u32 components[4]) {
    if (*p == 'v') p++;
    for (int i = 0; i < 3; i++) {
        if (!feed_number(&p, &components[i])) return 0;
        if (i < 2 && *p++ != '.') return 0;
    }
    if (!*p) {
        components[3] = 0xffffffffu;
        return 1;
    }
    if (p[0] != '-' || p[1] != 'p' || p[2] != 'r' || p[3] != 'e') return 0;
    p += 4;
    return feed_number(&p, &components[3]) && !*p;
}

static int feed_newer(const u32 version[4], const u32 current[4]) {
    for (int i = 0; i < 4; i++)
        if (version[i] != current[i]) return version[i] > current[i];
    return 0;
}

static int feed_parse(const char *input, const char *current_tag, int linux_desktop,
                      MayReleasePreview *release) {
    u32 latest[4];
    if (!feed_version(current_tag, latest)) return -1;
    const char *p = input;
    int header = 0, matched = 0, found = 0;
    while (*p) {
        const char *begin = p;
        while (*p && *p != '\n') p++;
        u32 length = (u32)(p - begin);
        if (!*p) break; /* do not consume a line cut by the bounded preview */
        p++;
        if (length && begin[length - 1] == '\r') length--;
        char line[512];
        if (!length || length >= sizeof(line) || *begin == '#') continue;
        memcpy(line, begin, length);
        line[length] = 0;
        if (!header) {
            header = !strcmp(line, "MAYOS_UPDATE_FEED_V1");
            continue;
        }
        char *part[10] = {line};
        int count = 1;
        for (u32 i = 0; i < length && count < 10; i++) {
            if (line[i] != '|') continue;
            line[i] = 0;
            part[count++] = line + i + 1;
        }
        if (count != 10 || strcmp(part[6], "x86_64") ||
            (linux_desktop ? (strcmp(part[1], "linux") && strcmp(part[1], "dual"))
                   : strcmp(part[1], "maykernel"))) continue;
        u32 version[4], bytes;
        const char *size = part[3];
        if (!feed_version(part[0], version) || !feed_number(&size, &bytes) || *size || !bytes ||
            strlen(part[7]) >= sizeof(release->kernel) || strlen(part[5]) != 10)
            continue;
        matched = 1;
        if (!feed_newer(version, latest)) continue;
        memcpy(latest, version, sizeof(latest));
        copy(release->tag, part[0], sizeof(release->tag));
        copy(release->name, part[2], sizeof(release->name));
        copy(release->date, part[5], sizeof(release->date));
        copy(release->kernel, part[7], sizeof(release->kernel));
        copy(release->summary, part[9], sizeof(release->summary));
        release->url[0] = release->sha256[0] = 0;
        if (strlen(part[8]) < sizeof(release->url) && strlen(part[4]) == 64) {
            copy(release->url, part[8], sizeof(release->url));
            copy(release->sha256, part[4], sizeof(release->sha256));
        }
        release->bytes = bytes;
        found = 1;
    }
    return matched ? found : -1;
}
#endif

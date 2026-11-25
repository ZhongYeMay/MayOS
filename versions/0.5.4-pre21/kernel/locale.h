#ifndef MAYOS_LOCALE_H
#define MAYOS_LOCALE_H

/* Keep display order independent of the BCP 47 identifiers written to disk. */
enum MayLocale {
    MAY_ZH_HANS, MAY_EN, MAY_ZH_HANT, MAY_JA, MAY_KO, MAY_ES, MAY_FR, MAY_DE,
    MAY_LOCALE_COUNT
};

typedef struct {
    const char *tag;
    const char *name;
} MayLanguage;

static const MayLanguage languages[MAY_LOCALE_COUNT] = {
    [MAY_ZH_HANS] = {"zh-Hans-CN", "简体中文"},
    [MAY_EN] = {"en-US", "English"},
    [MAY_ZH_HANT] = {"zh-Hant-TW", "繁體中文"},
    [MAY_JA] = {"ja-JP", "日本語"},
    [MAY_KO] = {"ko-KR", "한국어"},
    [MAY_ES] = {"es-ES", "Español"},
    [MAY_FR] = {"fr-FR", "Français"},
    [MAY_DE] = {"de-DE", "Deutsch"},
};

enum MayMessage {
#define MAY_MESSAGE(key, ...) MAY_MSG_##key,
#include "locale_strings.h"
#undef MAY_MESSAGE
    MAY_MSG_COUNT
};

static const char *const translations[MAY_MSG_COUNT][MAY_LOCALE_COUNT] = {
#define MAY_MESSAGE(key, ...) [MAY_MSG_##key] = {__VA_ARGS__},
#include "locale_strings.h"
#undef MAY_MESSAGE
};

#endif

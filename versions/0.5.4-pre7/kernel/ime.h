#ifndef MAYOS_IME_H
#define MAYOS_IME_H

#include "utf8.h"

typedef struct { const char *key, *words, *alternate; } MayImeEntry;
#include "ime_data.h"

enum MayImeMode {
    IME_LATIN, IME_PINYIN, IME_ZHUYIN, IME_HIRAGANA, IME_KATAKANA, IME_HANGUL, IME_UNICODE,
    IME_MODES
};
enum { IME_KEYS = 48, IME_TEXT = IME_KEYS * 4, IME_PAGE = 4 };
typedef struct {
    enum MayImeMode mode;
    char raw[IME_KEYS];
    unsigned size, selected;
} MayIme;

typedef struct {
    const char *packed;
    char first[IME_TEXT];
    unsigned count;
} MayImeChoices;

static const char *const ime_names[IME_MODES] = {
    "ABC", "拼音", "注音", "ひらがな", "カタカナ", "두벌식", "Unicode"
};

static void ime_clear(MayIme *ime) {
    ime->size = ime->selected = 0;
    ime->raw[0] = 0;
}

static const MayImeEntry *ime_find(const MayImeEntry *table, unsigned count, const char *key) {
    unsigned first = 0;
    while (first < count) {
        unsigned mid = first + (count - first) / 2;
        int comparison = strcmp(key, table[mid].key);
        if (!comparison) return &table[mid];
        if (comparison > 0) first = mid + 1;
        else count = mid;
    }
    return 0;
}

static unsigned ime_index(const char *characters, int key) {
    unsigned i = 0;
    while (characters[i] && characters[i] != key) i++;
    return characters[i] ? i + 1 : 0;
}

static unsigned ime_write(char *out, unsigned at, unsigned cp) {
    /* At most one codepoint per raw key. IME_TEXT allows four bytes per key. */
    at += may_utf8_put(out + at, cp);
    out[at] = 0;
    return at;
}

typedef struct { const char *keys; const char *kana; } MayKana;
static const MayKana kana[] = {
    {"kya","きゃ"},{"kyu","きゅ"},{"kyo","きょ"},{"gya","ぎゃ"},{"gyu","ぎゅ"},{"gyo","ぎょ"},
    {"sha","しゃ"},{"shi","し"},{"shu","しゅ"},{"sho","しょ"},{"sya","しゃ"},{"syu","しゅ"},{"syo","しょ"},
    {"ja","じゃ"},{"ji","じ"},{"ju","じゅ"},{"jo","じょ"},{"jya","じゃ"},{"jyu","じゅ"},{"jyo","じょ"},
    {"zya","じゃ"},{"zyu","じゅ"},{"zyo","じょ"},{"cha","ちゃ"},{"chi","ち"},{"chu","ちゅ"},{"cho","ちょ"},
    {"tya","ちゃ"},{"tyu","ちゅ"},{"tyo","ちょ"},{"tsu","つ"},{"dya","ぢゃ"},{"dyu","ぢゅ"},{"dyo","ぢょ"},
    {"nya","にゃ"},{"nyu","にゅ"},{"nyo","にょ"},{"hya","ひゃ"},{"hyu","ひゅ"},{"hyo","ひょ"},
    {"bya","びゃ"},{"byu","びゅ"},{"byo","びょ"},{"pya","ぴゃ"},{"pyu","ぴゅ"},{"pyo","ぴょ"},
    {"mya","みゃ"},{"myu","みゅ"},{"myo","みょ"},{"rya","りゃ"},{"ryu","りゅ"},{"ryo","りょ"},
    {"fa","ふぁ"},{"fi","ふぃ"},{"fe","ふぇ"},{"fo","ふぉ"},{"fu","ふ"},
    {"va","ゔぁ"},{"vi","ゔぃ"},{"vu","ゔ"},{"ve","ゔぇ"},{"vo","ゔぉ"},
    {"tsa","つぁ"},{"tsi","つぃ"},{"tse","つぇ"},{"tso","つぉ"},{"she","しぇ"},{"je","じぇ"},{"che","ちぇ"},
    {"ti","ち"},{"tu","つ"},{"thi","てぃ"},{"dhi","でぃ"},{"twu","とぅ"},{"dwu","どぅ"},
    {"kwa","くぁ"},{"kwi","くぃ"},{"kwe","くぇ"},{"kwo","くぉ"},{"wi","うぃ"},{"we","うぇ"},
    {"xa","ぁ"},{"xi","ぃ"},{"xu","ぅ"},{"xe","ぇ"},{"xo","ぉ"},{"xtsu","っ"},
    {"xya","ゃ"},{"xyu","ゅ"},{"xyo","ょ"},{"la","ぁ"},{"li","ぃ"},{"lu","ぅ"},{"le","ぇ"},{"lo","ぉ"},
    {"ltsu","っ"},{"lya","ゃ"},{"lyu","ゅ"},{"lyo","ょ"},
    {"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},{"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
    {"sa","さ"},{"si","し"},{"su","す"},{"se","せ"},{"so","そ"},{"za","ざ"},{"zi","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},
    {"ta","た"},{"te","て"},{"to","と"},{"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
    {"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},{"ha","は"},{"hi","ひ"},{"hu","ふ"},{"he","へ"},{"ho","ほ"},
    {"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},{"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
    {"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},{"ya","や"},{"yu","ゆ"},{"yo","よ"},
    {"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},{"wa","わ"},{"wo","を"},
    {"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"}
};

static void ime_kana(const char *raw, int katakana, char *out) {
    unsigned at = 0;
    for (const char *p = raw; *p;) {
        if (*p == 'n' && (!p[1] || p[1] == '\'' ||
            (p[1] != 'y' && !ime_index("aiueo", p[1])))) {
            at = ime_write(out, at, katakana ? 0x30f3 : 0x3093);
            p += (p[1] == '\'' || (p[1] == 'n' && !p[2])) ? 2 : 1;
            continue;
        }
        if ((*p == p[1] && ime_index("bcdfghjklmpqrstvwxyz", *p)) ||
            (*p == 't' && p[1] == 'c')) {
            at = ime_write(out, at, katakana ? 0x30c3 : 0x3063);
            p++;
            continue;
        }
        if (*p == '-') {
            at = ime_write(out, at, 0x30fc);
            p++;
            continue;
        }
        const MayKana *match = 0;
        unsigned length = 0;
        for (unsigned i = 0; i < sizeof(kana) / sizeof(kana[0]); i++) {
            unsigned j = 0;
            while (kana[i].keys[j] && p[j] == kana[i].keys[j]) j++;
            if (!kana[i].keys[j] && j > length) { match = &kana[i]; length = j; }
        }
        if (!match) {
            at = ime_write(out, at, (unsigned char)*p++);
            continue;
        }
        const char *reading = match->kana;
        while (*reading) at = ime_write(out, at, may_utf8_next(&reading) + (katakana ? 0x60 : 0));
        p += length;
    }
    out[at] = 0;
}

typedef struct { signed char left, right, result; } MayHangulPair;
static int ime_pair(const MayHangulPair *pairs, unsigned count, int left, int right) {
    for (unsigned i = 0; i < count; i++)
        if (pairs[i].left == left && pairs[i].right == right) return pairs[i].result;
    return -1;
}

static unsigned ime_hangul_char(int first, int vowel, int tail) {
    static const unsigned short leads[] = {
        0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,0x3145,
        0x3146,0x3147,0x3148,0x3149,0x314a,0x314b,0x314c,0x314d,0x314e
    };
    if (first >= 0 && vowel >= 0) return 0xac00 + (first * 21 + vowel) * 28 + tail;
    if (first >= 0) return leads[first];
    return vowel >= 0 ? 0x314f + vowel : 0;
}

static void ime_hangul(const char *raw, char *out) {
    static const char leads[] = "rRseEfaqQtTdwWczxvg";
    static const char vowels[] = "koiOjpuPh...yn...bm.l";
    static const signed char to_tail[] = {1,2,4,7,0,8,16,17,0,19,20,21,22,0,23,24,25,26,27};
    static const signed char tail_first[] = {0,0,0,1,0,4,4,0,0,8,8,8,8,8,8,8,0,0,17,0,0,0,0,0,0,0,0,0};
    static const signed char tail_lead[] = {-1,0,1,9,2,12,18,3,5,0,6,7,9,16,17,18,6,7,9,9,10,11,12,14,15,16,17,18};
    static const MayHangulPair mixed_vowels[] = {{8,0,9},{8,1,10},{8,20,11},{13,4,14},{13,5,15},{13,20,16},{18,20,19}};
    static const MayHangulPair mixed_tails[] = {{1,19,3},{4,22,5},{4,27,6},{8,1,9},{8,16,10},{8,17,11},{8,19,12},{8,25,13},{8,26,14},{8,27,15},{17,19,18}};
    int first = -1, vowel = -1, tail = 0;
    unsigned at = 0;
    for (const char *p = raw; *p; p++) {
        int l = (int)ime_index(leads, *p) - 1;
        int v = (int)ime_index(vowels, *p) - 1;
        if (v >= 0) {
            if (tail) {
                at = ime_write(out, at, ime_hangul_char(first, vowel, tail_first[tail]));
                first = tail_lead[tail];
                vowel = v;
                tail = 0;
            } else if (vowel < 0) {
                vowel = v;
            } else {
                int combined = ime_pair(mixed_vowels, sizeof(mixed_vowels)/sizeof(mixed_vowels[0]), vowel, v);
                if (combined >= 0) vowel = combined;
                else {
                    at = ime_write(out, at, ime_hangul_char(first, vowel, 0));
                    first = -1;
                    vowel = v;
                }
            }
        } else if (l >= 0) {
            int final = to_tail[l];
            if (first >= 0 && vowel >= 0 && final) {
                if (!tail) { tail = final; continue; }
                int combined = ime_pair(mixed_tails, sizeof(mixed_tails)/sizeof(mixed_tails[0]), tail, final);
                if (combined >= 0) { tail = combined; continue; }
            }
            at = ime_write(out, at, ime_hangul_char(first, vowel, tail));
            first = l;
            vowel = -1;
            tail = 0;
        } else {
            at = ime_write(out, at, ime_hangul_char(first, vowel, tail));
            at = ime_write(out, at, (unsigned char)*p);
            first = vowel = -1;
            tail = 0;
        }
    }
    ime_write(out, at, ime_hangul_char(first, vowel, tail));
}

static unsigned ime_hex_digit(int key) {
    if (key >= '0' && key <= '9') return (unsigned)(key - '0');
    if (key >= 'A' && key <= 'F') return (unsigned)(key - 'A' + 10);
    if (key >= 'a' && key <= 'f') return (unsigned)(key - 'a' + 10);
    return 16;
}

static void ime_convert(const MayIme *ime, int traditional, MayImeChoices *choices) {
    choices->first[0] = 0;
    choices->packed = 0;
    choices->count = 0;
    if (!ime->size) return;
    const MayImeEntry *entry;
#define IME_FIND(name) ime_find(ime_##name, sizeof(ime_##name) / sizeof(ime_##name[0]), ime->raw)
    switch (ime->mode) {
        case IME_PINYIN:
            entry = IME_FIND(pinyin);
            if (entry) choices->packed = traditional ? entry->alternate : entry->words;
            break;
        case IME_ZHUYIN:
            entry = IME_FIND(zhuyin);
            if (entry) choices->packed = entry->words;
            break;
        case IME_HIRAGANA:
            entry = IME_FIND(japanese);
            if (entry) choices->packed = entry->words;
            ime_kana(ime->raw, 0, choices->first);
            break;
        case IME_KATAKANA:
            ime_kana(ime->raw, 1, choices->first);
            break;
        case IME_HANGUL:
            ime_hangul(ime->raw, choices->first);
            break;
        case IME_UNICODE: {
            unsigned cp = 0;
            for (unsigned i = 0; i < ime->size; i++) cp = cp * 16 + ime_hex_digit(ime->raw[i]);
            if (cp >= 0x20 && !(cp >= 0x7f && cp <= 0x9f)) ime_write(choices->first, 0, cp);
            break;
        }
        default:
            break;
    }
#undef IME_FIND
    if (choices->first[0]) choices->count++;
    if (choices->packed && choices->packed[0]) {
        choices->count++;
        for (const char *p = choices->packed; *p; p++) if (*p == '|') choices->count++;
    }
}

static int ime_word(const MayImeChoices *choices, unsigned index, char *out) {
    if (index >= choices->count) return 0;
    if (choices->first[0]) {
        if (!index) { copy(out, choices->first, IME_TEXT); return 1; }
        index--;
    }
    const char *p = choices->packed;
    for (; index; index--) {
        while (*p != '|') p++;
        p++;
    }
    unsigned n = 0;
    while (p[n] && p[n] != '|' && n < IME_TEXT - 1) { out[n] = p[n]; n++; }
    out[n] = 0;
    return 1;
}

#endif

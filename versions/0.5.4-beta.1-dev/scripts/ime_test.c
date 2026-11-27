#include <assert.h>
#include <stdio.h>
#include <string.h>

static void copy(char *out, const char *in, unsigned cap) { snprintf(out, cap, "%s", in); }
#include "ime.h"

static void candidate(enum MayImeMode mode, const char *raw, unsigned index, const char *expect) {
    MayIme ime = {.mode = mode};
    ime_clear(&ime);
    copy(ime.raw, raw, sizeof(ime.raw));
    ime.size = strlen(raw);
    MayImeChoices choices;
    char output[IME_TEXT];
    ime_convert(&ime, 0, &choices);
    assert(ime_word(&choices, index, output));
    if (strcmp(expect,output)) fprintf(stderr, "%d %s expected %s got %s\n", mode, raw, expect, output);
    assert(!strcmp(expect, output));
}

int main(void) {
    candidate(IME_PINYIN, "nihao", 0, "你好");
    candidate(IME_PINYIN, "zhongguo", 0, "中国");
    candidate(IME_ZHUYIN, "su", 0, "你");
    candidate(IME_ZHUYIN, "5j/", 0, "中");
    candidate(IME_HIRAGANA, "nihongo", 0, "にほんご");
    candidate(IME_HIRAGANA, "nihongo", 1, "日本語");
    candidate(IME_HIRAGANA, "gakkou", 0, "がっこう");
    candidate(IME_KATAKANA, "ko-hi-", 0, "コーヒー");
    candidate(IME_HANGUL, "gksrmf", 0, "한글");
    candidate(IME_HANGUL, "dkssudgktpdy", 0, "안녕하세요");
    candidate(IME_HANGUL, "rhk", 0, "과");
    candidate(IME_HANGUL, "rkqtdl", 0, "값이");
    candidate(IME_HANGUL, "rkqtl", 0, "갑시");
    candidate(IME_UNICODE, "1f469", 0, "👩");
    const char *cursor = "\xf0\x9f\x91\xa9";
    assert(may_utf8_next(&cursor) == 0x1f469 && !*cursor);
    cursor = "\xed\xa0\x80";
    assert(may_utf8_next(&cursor) == 0xfffd && (unsigned char)*cursor == 0xa0);
    char value[] = "A👩‍💻";
    may_utf8_backspace(value);
    assert(!strcmp(value, "A"));
    char combining[] = "Aか\xcc\x81";
    may_utf8_backspace(combining);
    assert(!strcmp(combining, "A"));
    puts("PASS: Pinyin, Zhuyin, romaji/kana/kanji, Dubeolsik and Unicode");
}

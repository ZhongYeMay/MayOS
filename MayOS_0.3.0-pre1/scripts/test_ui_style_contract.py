#!/usr/bin/env python3
"""Prevent unapproved visual redesigns of the original MayOS Material workspace.

The eight drawing/widget functions below are approved as they appeared on
main before PR #9. No external dependencies or network are needed.
This is a guard for the default UI style, not a screenshot/QEMU acceptance test.
"""
from pathlib import Path

GUI = Path(__file__).resolve().parents[1] / "kernel" / "gui.c"
source = GUI.read_text(encoding="utf-8")
function_hashes = {
    "static void rect(": 0x54B4C11C,
    "static void border(": 0x4E06F312,
    "static void roundrect(": 0x91EE82FD,
    "static void circle(": 0x0217F223,
    "static int text(": 0x36BDBD85,
    "static int large_text(": 0x8849B15F,
    "static void button(": 0xE967CA53,
    "static void frame(": 0x3C421ED8,
}


def function_body(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise ValueError("unbalanced function " + signature)


def fnv_ucs2(text):
    value = 2166136261
    for char in text:
        value = ((value ^ ord(char)) * 16777619) & 0xFFFFFFFF
    return value


for signature, expected in function_hashes.items():
    got = fnv_ucs2(function_body(signature))
    assert got == expected, f"{signature}: original widget style changed {got:#x}"
assert "static int dark = 1, accent_choice = 0;" in source
assert "static int desktop_style = 0, pointer_size = 0, show_brand = 1, high_contrast = 0;" in source
assert "static int cursor_style = 0;" in source
assert "bg = dark ? 0x141218 : 0xfffbfe;" in source
assert "panel = dark ? 0x211f26 : 0xfff7ff;" in source
assert "surface_high = dark ? 0x2b2930 : 0xf3edf7;" in source
assert "set gfxmode" not in source  # no graphics firmware tweaks via GUI
assert "static void cursor(void)" in source
assert 'const char *shapes[] = {"■ 黑色方块", "□ 白色方块", "自定义图片"};' in source
print("PASS: original MayOS window geometry, renderer and Material defaults retained; only requested pointer controls added")

#!/usr/bin/env python3
"""Verify the newly approved MayOS 0.5.0 monochrome design without losing function.

This contract supersedes the earlier historical 0.4.0-pre3 visual pixels, not
its fixed click zones and native file/setting behavior. Real QEMU acceptance
and screenshot verification run separately.
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
# Guard preserved renderer primitives and the approved framebuffer UI.
assert "static int dark = 1, accent_choice = 0;" in source
assert "static int desktop_style = 0, pointer_size = 0, show_brand = 1, high_contrast = 0;" in source
assert "static int cursor_style = 0;" in source
assert "bg = dark ? 0x050506 : 0xf5f5f6;" in source
assert "panel = dark ? 0x080809 : 0xffffff;" in source
assert "line = dark ? 0x303033 : 0xd9d9dd;" in source
assert "static void soft_roundrect(" in source
assert "static void ui_card(" in source
assert "static void reference_monitor_icon(" in source
assert "static void reference_text(" in source
assert "premium_title_bitmap[g]" in source
assert "premium_body_bitmap[g]" in source
assert "premium_menu_bitmap[g]" in source
assert "static void reference_nav_icon(" in source
assert "static void reference_search_icon(" in source
assert "static void settings_fit_window(" in source
assert "static int settings_nav_y(" in source
assert "static int settings_filter_match(" in source
assert "settings_search_focused = 1" in source

# Desktop: pure-black menu, bright vector status glyphs, two soft-wave bands,
# native icons, and four-app floating dock with unchanged shortcuts.
assert 'reference_text(52, 2, "MayOS", ink, 0);' in source
assert 'const char *menus[] = {"Settings", "File", "Edit", "View", "Window", "Help"};' in source
assert 'reference_search_icon(W - 320, 6, ink);' in source
assert 'reference_battery_icon(W - 204, 5, ink);' in source
assert 'int upper = 330 - x * 2 / 3 + x * x / 4500;' in source
assert 'int lower = 820 - (x - 450) * (x - 450) / 1150;' in source
assert "static void taskbar(void)" in source
assert "w->x = (W - 824) / 2;" in source
assert "int left = (W - 244) / 2, top = H - 92;" in source
assert "ui_card(left, top, 244, 70, 24," in source
assert 'const int centers[] = {first, first + 57, first + 114, first + 171};' in source

# Settings: verify geometry, proportions and actual click targets use the
# same navigation function. Screenshot-only controls are not acceptable.
assert '{(W - 824) / 2, 110, 824, 480, 0, 0, 0, 0, 0, 0, 0, "Settings"}' in source
assert 'int x = w->x, y = w->y, cx = x + 252, cy = y + 55, cw = w->w - 282;' in source
assert 'reference_text(x + 24, y + 49, "Settings", ink, 1);' in source
assert 'ui_card(x + 22, y + 95, 202, 38, 19, search_bg);' in source
assert 'reference_text(x + 67, yy + 7, tabs[i],' in source
assert 'rect(x + 226, y + 41, 1, w->h - 62, line);' in source
assert 'settings_nav_y(w, i)' in source
assert 'settings_fit_window(w);' in source
assert 'inside(rx, ry, 22, 95, 202, 38)' in source
assert '"Privacy & Security", "Updates", "About MayOS"' in source
assert 'reference_text(cx, cy - 12, "About MayOS", ink, 1);' in source
assert 'ui_card(cx, card_top, cw, card_height, 23, card_bg);' in source
assert 'reference_monitor_icon(cx + 23, card_top + 31, ink, card_bg);' in source
assert 'reference_text(cx + 68, card_top + 13, "MayOS", ink, 1);' in source
assert 'reference_text(cx + 68, card_top + 51, "Version " MAYOS_RELEASE, muted, 0);' in source
assert 'reference_right_label(cx + cw - 23, card_top + 122, "MayKernel x86_64", ink);' in source
assert 'reference_right_label(cx + cw - 23, card_top + 157, "Development Preview", ink);' in source
assert 'reference_right_label(cx + cw - 23, card_top + 192, "64-bit", ink);' in source
assert 'u32 card_bg = dark ? 0x000000 : 0xffffff;' in source
assert "static void cursor(void)" in source
assert "#include \"material_icons.h\"" in source
assert "#if MAYOS_MATERIAL_ICONS" in source
assert "accent = dark ? dark_primary[accent_choice] : light_primary[accent_choice];" in source
assert 'const char *shapes[] = {"■ 黑色方块", "□ 白色方块", "自定义图片"};' in source
assert 'fs_find(2, "Notes.txt")' in source

# Existing current release is immutable; while this redesign is on a
# development branch the source must continue reporting its actual version.
version_header = (GUI.parent / "version.h").read_text(encoding="utf-8")
assert '#define MAYOS_VERSION "0.5.0-pre2-dev"' in version_header
assert '#include "version.h"' in source
assert '#define MAYOS_RELEASE MAYOS_VERSION' in source
assert 'emit("OS: MayOS " MAYOS_RELEASE);' in source
print("PASS: reference-driven native MayOS redesign, geometry, real search/navigation, four-icon dock and preserved renderer")

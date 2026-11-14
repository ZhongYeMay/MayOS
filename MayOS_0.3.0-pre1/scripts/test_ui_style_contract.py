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
assert "static int dark = 1, accent_choice = 0;" in source
assert "static int desktop_style = 0, pointer_size = 0, show_brand = 1, high_contrast = 0;" in source
assert "static int cursor_style = 0;" in source
assert "bg = dark ? 0x050506 : 0xf5f5f6;" in source
assert "panel = dark ? 0x080809 : 0xffffff;" in source
assert "surface_high = dark ? 0x18181b : 0xf0f0f2;" in source
assert "line = dark ? 0x303033 : 0xd9d9dd;" in source
assert "static void soft_roundrect(" in source
assert "static int heading_text(" in source
assert 'heading_text(42, 4, "MayOS", ink);' in source
assert 'text(101, 8, "Settings", muted);' in source
assert 'text(164, 8, "File", muted);' in source
assert 'text(205, 8, "Edit", muted);' in source
assert 'text(244, 8, "View", muted);' in source
assert 'text(286, 8, "Window", muted);' in source
assert 'text(348, 8, "Help", muted);' in source
assert 'text(907, 8, "Tue Jan 28  10:24", muted);' in source
assert 'heading_text(x + 20, y + 49, "Settings", ink);' in source
assert '"Privacy & Security", "Updates", "About MayOS"' in source
assert 'heading_text(cx, cy - 8, "About MayOS", ink);' in source
assert 'reference_text(cx + 57, card_top + 18, "MayOS", ink, 1);' in source
assert "static void ui_card(" in source
assert "static void reference_monitor_icon(" in source
assert "static void reference_text(" in source
assert "static int reference_font_width(" in source
assert "premium_title_bitmap[g]" in source
assert "premium_body_bitmap[g]" in source
assert "static void reference_right_label(" in source
assert 'reference_monitor_icon(cx + 18, card_top + 31, ink, card_bg);' in source
assert 'reference_text(cx + 18, card_top + 126, "Kernel", muted, 0);' in source
assert 'reference_right_label(cx + cw - 18, card_top + 126, "MayKernel x86_64", ink);' in source
assert 'reference_right_label(cx + cw - 18, card_top + 190, "64-bit", ink);' in source
assert 'u32 card_bg = dark ? 0x000000 : 0xffffff;' in source
assert "ui_card(w->x, w->y, w->w, w->h, 25, panel);" in source
assert "ui_card(cx, card_top, cw, card_height, 24, card_bg);" in source
assert 'reference_text(cx + 57, card_top + 48, "Version " MAYOS_RELEASE, muted, 0);' in source
assert 'reference_right_label(cx + cw - 18, card_top + 158, "Development Preview", ink);' in source
assert "ui_card(left, top, 244, 70, 24," in source
assert "set gfxmode" not in source  # no graphics firmware tweaks via GUI
assert "static void cursor(void)" in source
assert "#include \"material_icons.h\"" in source
assert "#if MAYOS_MATERIAL_ICONS" in source
assert "accent = dark ? dark_primary[accent_choice] : light_primary[accent_choice];" in source
assert "0xe2e2e7" in source
# Version is centralized; updating it must not count as a redesign.
version_header = (GUI.parent / "version.h").read_text(encoding="utf-8")
assert '#define MAYOS_VERSION "0.5.0-pre1"' in version_header
assert '#include "version.h"' in source
assert '#define MAYOS_RELEASE MAYOS_VERSION' in source
assert 'reference_text(cx + 57, card_top + 48, "Version " MAYOS_RELEASE, muted, 0);' in source
assert 'emit("OS: MayOS " MAYOS_RELEASE);' in source
assert 'const char *shapes[] = {"■ 黑色方块", "□ 白色方块", "自定义图片"};' in source
assert "roundrect(14, 60, 76, 508, panel);" not in source
assert "static void desktop(void)" in source
assert "static void taskbar(void)" in source
assert "int left = 382, top = H - 92;" in source
assert "int upper = 128 - x / 3 + (x * x) / 4200;" in source
assert '{177, 133, 640, 450, 0, 0, 0, 0, 0, 0, 0, "Settings"}' in source
assert "settings_tab = 6" in source
print("PASS: exact approved monochrome Settings reference, menu, wallpaper, About card and four-icon Dock")

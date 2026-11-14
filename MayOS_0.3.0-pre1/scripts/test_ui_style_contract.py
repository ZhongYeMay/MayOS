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
assert 'heading_text(24, 6, "MayOS", ink);' in source
assert 'heading_text(x + 22, y + 60, "Settings", ink);' in source
assert 'heading_text(cx, cy - 8, "About MayOS", ink);' in source
assert 'heading_text(cx + 78, cy + 118, "MayOS", ink);' in source
assert "static void ui_card(" in source
assert "ui_card(w->x, w->y, w->w, w->h, 25, panel);" in source
assert "ui_card(cx, cy + 105, cw, 289, 22, panel);" in source
assert "text(cx + 78, cy + 152, \"Version \" MAYOS_RELEASE, muted);" in source
assert "text(cx + 280, cy + 250, \"Development Preview\", ink);" in source
assert "ui_card(left, top, 604, 76, 27," in source
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
assert 'text(cx + 78, cy + 152, "Version " MAYOS_RELEASE, muted);' in source
assert 'emit("OS: MayOS " MAYOS_RELEASE);' in source
assert 'const char *shapes[] = {"■ 黑色方块", "□ 白色方块", "自定义图片"};' in source
assert "roundrect(14, 60, 76, 508, panel);" not in source
assert "static void desktop(void)" in source
assert "static void taskbar(void)" in source
assert 'text(126, 10, "文件", muted);' in source
assert 'text(200, 10, "窗口", muted);' in source
assert 'text(274, 10, "应用", muted);' in source
assert 'text(348, 10, "终端", muted);' in source
assert "int left = 210, top = H - 84;" in source
assert "int high = 105 + (x * x) / 2200 + x / 8;" in source
assert 'fs_find(2, "Notes.txt")' in source
assert '{186, 74, 800, 620, 0, 0, 0, 0, 0, 0, 0, "系统设置 / System Settings"}' in source
print("PASS: approved 0.5 premium monochrome visual contract, AA cards, About layout, macOS menu, dock geometry and input hotspots")

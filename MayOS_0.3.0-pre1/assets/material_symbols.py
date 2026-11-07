#!/usr/bin/env python3
"""Generate native icon bitmaps from Apache-2.0 Google Material Symbols Rounded.

The exact upstream Git commit is pinned for reproducibility. The OS embeds only
ten 48x48 grayscale glyph bitmaps; it never fetches fonts/CDNs at runtime.
If the build is offline without cached assets, the existing native vector
symbols remain available. Release CI must verify that real glyphs were built.
"""
from __future__ import annotations

from pathlib import Path
import os
import urllib.request

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
OUT = BUILD / "material_icons.h"
FONT = BUILD / "MaterialSymbolsRounded.ttf"
COMMIT = "481507587f1bdfe712939398c4dc0ecc2079ea7c"
URL = (
    "https://raw.githubusercontent.com/google/material-design-icons/"
    f"{COMMIT}/variablefont/"
    "MaterialSymbolsRounded%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf"
)
# Codepoints from the pinned MaterialSymbolsRounded .codepoints manifest.
# Index corresponds to gui.c symbol() kind.
ICONS = (
    ("folder", 0xe2c7),
    ("settings", 0xe8b8),
    ("terminal", 0xeb8e),
    ("delete", 0xe92e),
    ("public", 0xe80b),
    ("description", 0xe873),
    ("apps", 0xe5c3),
    ("notifications", 0xe7f5),
    ("wifi", 0xe63e),
    ("brightness_6", 0xe3ab),
)

def main() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    supplied = os.getenv("MAYOS_MATERIAL_FONT")
    font = Path(supplied) if supplied else FONT
    if not font.is_file() and not os.getenv("MAYOS_MATERIAL_OFFLINE"):
        try:
            request = urllib.request.Request(URL, headers={
                "User-Agent": "MayOS-icon-rasterizer/0.4"
            })
            with urllib.request.urlopen(request, timeout=45) as src:
                data = src.read(20 * 1024 * 1024 + 1)
            if not 1_000_000 < len(data) < 20 * 1024 * 1024:
                raise RuntimeError("Unreasonable Material Symbols font size")
            FONT.write_bytes(data)
            font = FONT
        except (OSError, RuntimeError) as exc:
            print("Material Symbols download unavailable:", exc, flush=True)
    if not font.is_file():
        OUT.write_text(
            "/* Offline: use built-in native vector icons instead. */\n"
            "#define MAYOS_MATERIAL_ICONS 0\n"
        )
        print("Material Symbols unavailable: native vector fallback", flush=True)
        return
    try:
        face = ImageFont.truetype(str(font), 42)
        bitmaps = []
        for name, codepoint in ICONS:
            image = Image.new("L", (48, 48), 0)
            draw = ImageDraw.Draw(image)
            draw.text((24, 24), chr(codepoint), font=face, fill=255, anchor="mm")
            pixels = image.tobytes()
            if sum(pixels) < 1000:
                raise RuntimeError(f"Missing glyph {name} ({codepoint:x})")
            bitmaps.append(pixels)
    except (OSError, RuntimeError, ValueError) as exc:
        raise RuntimeError("Material Symbols was downloaded but cannot render") from exc
    with OUT.open("w") as out:
        out.write(
            "/* Official Google Material Symbols Rounded; Apache-2.0. */\n"
            "/* Upstream: google/material-design-icons, pinned commit "
            f"{COMMIT}. */\n"
            "#define MAYOS_MATERIAL_ICONS 1\n"
            "static const unsigned char material_icons[10][2304] = {\n"
        )
        for bitmap in bitmaps:
            out.write("{" + ",".join(str(pixel) for pixel in bitmap) + "},\n")
        out.write("};\n")
    print("Material Symbols Rounded: 10 licensed glyphs rasterized", flush=True)

if __name__ == "__main__":
    main()

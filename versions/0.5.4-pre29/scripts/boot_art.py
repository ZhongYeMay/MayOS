#!/usr/bin/env python3
"""Prepare the supplied MayOS brand for the GRUB menu and kernel splash."""
import argparse
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "assets/limine-mayos-brand.png"


def logo(width):
    source = Image.open(BRAND).convert("L")
    bounds = source.point(lambda value: 255 if value > 24 else 0).getbbox()
    if bounds is None:
        raise ValueError(f"Empty boot logo: {BRAND}")
    source = source.crop(bounds)
    source.thumbnail((width, width), Image.Resampling.LANCZOS)
    return source


def make_theme_colors():
    palette = ROOT / "kernel/design_palette.h"
    values = re.findall(r"^#define (MAY_DARK_[A-Z_]+) 0x([0-9a-f]{6})$", palette.read_text(), re.M)
    return {key: "#" + rgb for key, rgb in values}


def menu_background(destination, colors):
    canvas = Image.new("RGB", (1280, 720), colors["MAY_DARK_BACKGROUND"])
    mark = logo(213)
    canvas.paste(colors["MAY_DARK_INK"], ((canvas.width - mark.width) // 2, 37), mark)
    canvas.save(destination)


def menu_selection(directory, colors):
    image = Image.new("RGBA", (21, 17))
    ImageDraw.Draw(image).rounded_rectangle((0, 0, 20, 16), radius=6,
                                           fill=colors["MAY_DARK_SELECTION"], outline=colors["MAY_DARK_LINE"])
    horizontal = (0, 8, 13, 21)
    vertical = (0, 5, 12, 17)
    for y, row in enumerate((("nw", "n", "ne"), ("w", "c", "e"), ("sw", "s", "se"))):
        for x, part in enumerate(row):
            image.crop((horizontal[x], vertical[y], horizontal[x + 1], vertical[y + 1])).save(
                directory / f"selection_{part}.png")


def spinner_frame(frame, frames, size):
    """Draw a rotating arc with rounded ends on an oversampled transparent mask."""
    scale = 4
    radius = 16 * scale
    stroke = 4 * scale
    center = size * scale // 2
    phase = frame / frames
    sweep = 55 + 135 * (1 - math.cos(math.tau * phase)) / 2
    angle = 360 * phase - 90
    mask = Image.new("L", (size * scale, size * scale))
    draw = ImageDraw.Draw(mask)
    outside = radius + stroke // 2
    draw.arc((center - outside, center - outside, center + outside, center + outside),
             angle, angle + sweep, fill=255, width=stroke)
    for degrees in (angle, angle + sweep):
        theta = math.radians(degrees)
        x = center + radius * math.cos(theta)
        y = center + radius * math.sin(theta)
        half = stroke // 2
        draw.ellipse((x - half, y - half, x + half, y + half), fill=255)
    return mask.resize((size, size), Image.Resampling.LANCZOS).tobytes()


def write_mask(header, name, pixels):
    header.write(f"static const unsigned char {name}[] = {{\n")
    for first in range(0, len(pixels), 24):
        header.write("    " + ", ".join(map(str, pixels[first:first + 24])) + ",\n")
    header.write("};\n")


def splash_header(destination):
    mark = logo(245)
    frames, size = 64, 44
    with destination.open("w", encoding="ascii") as header:
        header.write(f"#define BOOT_LOGO_W {mark.width}\n#define BOOT_LOGO_H {mark.height}\n")
        header.write(f"#define BOOT_SPINNER_FRAMES {frames}\n#define BOOT_SPINNER_SIZE {size}\n")
        write_mask(header, "boot_logo", mark.tobytes())
        write_mask(header, "boot_spinner",
                   b"".join(spinner_frame(i, frames, size) for i in range(frames)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    splash_header(parser.parse_args().output)

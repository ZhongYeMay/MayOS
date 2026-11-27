#!/usr/bin/env python3
"""Offline Bing wallpaper regression: no copyrighted image is checked in."""
from pathlib import Path
import hashlib
import shlex
import subprocess
import tempfile
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
W, H = 1280, 720

with tempfile.TemporaryDirectory(prefix="mayos-bing-test-") as tmp:
    root = Path(tmp)
    worker = root / "mayos-wallpaper"
    libs = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "libcurl", "libjpeg"], text=True))
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(ROOT / "linux/wallpaper.c"), *libs, "-o", str(worker)],
                   check=True)
    image = Image.new("RGB", (1920, 1080), (245, 21, 18))
    ImageDraw.Draw(image).rectangle((960, 0, 1919, 1079), fill=(12, 40, 232))
    jpeg = root / "test.jpg"
    image.save(jpeg, quality=95)
    bitmap, date = root / "daily.r565", root / "daily.date"
    subprocess.run([str(worker), str(bitmap), str(date), "--fixture-jpeg", str(jpeg)], check=True)
    data = bitmap.read_bytes()
    assert len(data) == W * H * 2
    def pixel(x, y):
        index = 2 * (y * W + x)
        return int.from_bytes(data[index:index + 2], "little")
    left, right = pixel(100, 200), pixel(1100, 200)
    assert (left >> 11) >= 27 and (left & 31) <= 5, hex(left)
    assert (right & 31) >= 25 and (right >> 11) <= 5, hex(right)
    assert date.read_text() == "19700101\n"
    original = hashlib.sha256(data).hexdigest()
    jpeg.write_bytes(b"not a jpeg: must never replace existing wallpaper")
    failed = subprocess.run([str(worker), str(bitmap), str(date), "--fixture-jpeg", str(jpeg)])
    assert failed.returncode != 0
    assert hashlib.sha256(bitmap.read_bytes()).hexdigest() == original
    assert date.read_text() == "19700101\n"
    subprocess.run(["sh", "-n", str(ROOT / "linux/wallpaper-service")], check=True)
    source = (ROOT / "linux/wallpaper-service").read_text()
    assert "mayos.persist=" in source or "MAYOS_PROFILE_DIR" in source
    for dangerous in ("mkfs", "fdisk", "wipefs", "parted"):
        assert dangerous not in source
print("PASS: real libjpeg RGB565 conversion, atomic fallback and no-disk-format service")

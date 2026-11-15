#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
config = (root / "boot/limine/limine.conf").read_text(encoding="utf-8")
for line in ("interface_resolution: 1280x720", "wallpaper: boot():/boot/mayos-brand.png",
             "interface_branding: MayOS · Limine Boot Manager", "/Start MayKernel64",
             "protocol: multiboot1", "/Start Linux64", "protocol: linux",
             "resolution: 1280x720x32"):
    assert line in config, line
assert (root / "assets/limine-mayos-brand.png").is_file()
print("PASS: Limine 1280x720 branding and dual-kernel menu contract")

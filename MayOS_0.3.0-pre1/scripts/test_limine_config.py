#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
config = (root / "boot/limine/limine.conf").read_text(encoding="utf-8")
for line in ("INTERFACE_RESOLUTION=1280x720", "BACKGROUND_PATH=boot():/boot/mayos-brand.png",
             "/Start MayKernel64", "PROTOCOL=multiboot", "/Start Linux64",
             "PROTOCOL=linux", "/Reboot", "/Shutdown"):
    assert line in config, line
assert (root / "assets/limine-mayos-brand.png").is_file()
print("PASS: Limine 1280x720 branding and dual-kernel menu contract")

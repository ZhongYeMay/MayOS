#!/usr/bin/env python3
"""Keep the branded GRUB boot path present in both ISO staging layouts."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for config in (root / "boot/grub/grub.cfg", root / "boot/grub/dual.cfg.in"):
    text = config.read_text(encoding="utf-8")
    assert "1280x720x32" in text
    assert "insmod gfxterm" in text
    assert "insmod png" in text
    assert "set theme=/boot/grub/themes/mayos/theme.txt" in text
assert (root / "boot/grub/themes/mayos/theme.txt").is_file()
assert (root / "assets/limine-mayos-brand.png").is_file()
print("PASS: branded GRUB theme contract for BIOS, UEFI and both kernels")

#!/usr/bin/env python3
"""Keep the branded GRUB boot path present in both ISO staging layouts."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for config in (root / "boot/grub/grub.cfg", root / "boot/grub/dual.cfg.in"):
    assert "source /boot/grub/menu.cfg" in config.read_text(encoding="utf-8")
menu = (root / "boot/grub/menu.cfg").read_text(encoding="utf-8")
assert "set timeout=5\n" in menu
assert "1280x720x32" in menu and "640x480" in menu
assert "loadfont /boot/grub/fonts/mayos-menu.pf2" in menu
assert "set theme=/boot/grub/themes/mayos/theme.txt" in menu
theme = (root / "boot/grub/themes/mayos/theme.txt").read_text(encoding="utf-8")
assert 'id = "__timeout__"' in theme
assert 'desktop-image-scale-method: "padding"' in theme
assert (root / "assets/limine-mayos-brand.png").is_file()
print("PASS: branded GRUB theme contract for BIOS, UEFI and both kernels")

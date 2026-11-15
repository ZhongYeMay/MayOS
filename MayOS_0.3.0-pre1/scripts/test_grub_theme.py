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
    assert 'menuentry "MayOS requires an x86_64 CPU"' in text
    assert 'echo "MayOS requires an x86_64 CPU"' in text
    assert "set default=0" in text or config.name == "dual.cfg.in"
dual = (root / "boot/grub/dual.cfg.in").read_text()
assert dual.index("serial --unit=0") < dual.index("terminal_output gfxterm serial")
assert 'menuentry "Start MayKernel64"' in dual
assert 'menuentry "Start Linux64"' in dual
assert "else\n# A named 64-bit default" in dual
assert "set default=0\nmenuentry \"MayOS requires an x86_64 CPU\"" in dual
assert (root / "boot/grub/themes/mayos/theme.txt").is_file()
assert (root / "assets/limine-mayos-brand.png").is_file()
print("PASS: branded GRUB theme contract for BIOS, UEFI and both kernels")

#!/usr/bin/env python3
"""CI-only: add a conspicuous QEMU test opt-in to a disposable GRUB config.

Never commit the modified boot/grub/dual.cfg.in to an official ISO; the
normal user media has no hardcoded disk device. This test config is generated
only on the Actions runner.
"""
from pathlib import Path
cfg = Path(__file__).resolve().parents[1] / "boot/grub/dual.cfg.in"
source = cfg.read_text()
needle = 'menuentry "Linux @LINUX_VERSION@ - rescue shell (x86_64)"'
if source.count(needle) != 1 or "mayos-linux-persistent-qemu-test" in source:
    raise SystemExit("unexpected GRUB template / duplicate test menu")
entry = """menuentry "Linux @LINUX_VERSION@ - QEMU PERSISTENCE TEST ONLY (/dev/sda)" --id mayos-linux-persistent-qemu-test {
    set gfxpayload=keep
    linux /boot/linux/bzImage console=tty0 console=ttyS0,115200 quiet rdinit=/init mayos.desktop=1 mayos.persist=/dev/sda panic=10
    initrd /boot/linux/initramfs.cpio.gz
    boot
}

"""
cfg.write_text(source.replace(needle, entry + needle, 1))
print("PASS: injected disposable opt-in QEMU-only GRUB test menu")

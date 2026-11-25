#!/usr/bin/env python3
"""Verify the settings UI actually changes the scanout and safely reverts it."""
import argparse
from pathlib import Path
import shutil
import tempfile

from PIL import Image

from desktop_menu_smoke import application, menu
from dual_boot_smoke import BootVM


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--initramfs", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mayos-kms-") as root:
        disk = Path(root) / "blank.img"
        with disk.open("xb") as image:
            image.truncate(8 * 1024 * 1024)
        vm = BootVM(iso=None, disk=disk, tag="linux-kms", qemu=shutil.which("qemu-system-x86_64"),
                    memory=512, timeout=50, extra_args=(
                        "-kernel", str(args.kernel.resolve()), "-initrd", str(args.initramfs.resolve()),
                        "-append", "console=tty0 console=ttyS0,115200 quiet rdinit=/init mayos.desktop=1 vga=0x315",
                    ))

        def size(shot):
            with Image.open(vm.shot(shot)) as image:
                return image.size

        def action(x, marker, timeout=8):
            offset = len(vm.text)
            vm.click(x, 511)
            vm.wait("MayOS display: DRM/KMS " + marker, offset=offset, timeout=timeout)

        try:
            vm.wait("MayOS display: DRM/KMS active display detected")
            vm.wait("MayOS: desktop ready")
            application(vm, "settings", lambda: menu(vm, 0, 2))
            menu(vm, 2, 1)
            original = size("linux-display-original")

            action(724, "selected monitor mode activated")
            candidate = size("linux-display-preview")
            assert candidate != original, (original, candidate)
            action(554, "previous mode restored")
            assert size("linux-display-reverted") == original

            action(724, "selected monitor mode activated")
            vm.wait("MayOS display: DRM/KMS previous mode restored", offset=vm.text.rfind(
                "MayOS display: DRM/KMS selected monitor mode activated"), timeout=22)
            assert size("linux-display-timeout") == original

            action(724, "selected monitor mode activated")
            action(724, "selected mode kept")
            assert size("linux-display-kept") == candidate
        finally:
            vm.stop()
    print(f"PASS: real Linux DRM/KMS scanout {original} → {candidate}; manual revert, automatic revert and keep")


if __name__ == "__main__":
    main()

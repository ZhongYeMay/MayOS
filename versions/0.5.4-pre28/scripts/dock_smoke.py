#!/usr/bin/env python3
"""Exercise the Dock's clicks and window actions through real mouse events."""
import argparse
from pathlib import Path
import shutil
import tempfile
import time

from dual_boot_smoke import BUILD, BootVM


def dock_x(slot, count):
    return (1280 - (30 + count * 57)) // 2 + 34 + slot * 57 + (14 if slot == count - 1 else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=BUILD / "mayos.iso")
    parser.add_argument("--linux-kernel", type=Path)
    parser.add_argument("--linux-initramfs", type=Path)
    args = parser.parse_args()
    if bool(args.linux_kernel) != bool(args.linux_initramfs):
        parser.error("supply both Linux boot files")
    extra = (
        "-kernel", str(args.linux_kernel), "-initrd", str(args.linux_initramfs),
        "-append", "console=tty0 console=ttyS0,115200 quiet rdinit=/init mayos.desktop=1 vga=0x315",
    ) if args.linux_kernel else ()
    with tempfile.TemporaryDirectory(prefix="mayos-dock-") as folder:
        disk = Path(folder) / "empty.img"
        with disk.open("xb") as image:
            image.truncate(8 * 1024 * 1024)
        vm = BootVM(iso=None if extra else args.iso.resolve(), disk=disk, tag="dock-linux" if extra else "dock-native",
                    qemu=shutil.which("qemu-system-x86_64"), memory=512, timeout=45, extra_args=extra)
        try:
            if not extra:
                vm.wait("MayKernel64 display modes")
                vm.key("ret")
            vm.wait("MayOS: desktop ready")
            vm.wait("lease acquired" if not extra else "MayOS network: eth0 leased")
            time.sleep(.45)

            def click(slot, count, marker, y=672):
                start = len(vm.text)
                vm.click(dock_x(slot, count), y)
                vm.wait(marker, offset=start, timeout=8)

            click(0, 5, "MayOS: application launcher opened")
            click(1, 5, "MayOS: active application menu: files")
            vm.click(57, 17)
            click(2, 5, "MayOS: active application menu: settings", y=702)
            click(3, 5, "MayOS: active application menu: terminal")
            click(4, 5, "MayOS: trash opened")
            vm.shot("dock-linux-trash" if extra else "dock-native-trash")
            click(1, 5, "MayOS: launcher application opened")
            vm.key("f2")
            vm.wait("MayOS: Task Manager opened")
            click(2, 6, "MayOS: active application menu: settings")
            click(4, 6, "MayOS: active application menu: task-manager")
            vm.click(dock_x(4, 6), 672, "right")
            vm.shot("dock-linux-window-menu" if extra else "dock-native-window-menu")
            start = len(vm.text)
            vm.click(dock_x(4, 6), 579)
            vm.wait("MayOS: active application menu: settings", offset=start, timeout=8)
            click(4, 6, "MayOS: active application menu: task-manager")
            vm.click(dock_x(4, 6), 672, "right")
            start = len(vm.text)
            vm.click(dock_x(4, 6), 608)
            vm.wait("MayOS: active application menu: settings", offset=start, timeout=8)
            click(3, 5, "MayOS: active application menu: terminal")
        finally:
            vm.stop()
    print("PASS: five Dock shortcuts, first click through an open menu, application restore/minimize/close", flush=True)


if __name__ == "__main__":
    main()

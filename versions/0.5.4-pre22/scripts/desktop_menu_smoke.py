#!/usr/bin/env python3
"""Exercise the actual application menu bar and a persisted menu command."""
import argparse
from pathlib import Path
import shutil
import tempfile
import time

from dual_boot_smoke import BUILD, BootVM
from smoke import entries


def press(vm, key):
    vm.key(key)
    # Give the emulated desktop time to render and drain the input queue.
    time.sleep(0.12)


def menu(vm, offset, row=None):
    vm.click(56, 16)  # The MayOS system menu is always first.
    time.sleep(0.15)
    for _ in range(offset):
        press(vm, "right")
    if row is not None:
        for _ in range(row + 1):
            press(vm, "down")
        press(vm, "ret")


def application(vm, name, action):
    offset = len(vm.text)
    action()
    vm.wait(f"MayOS: active application menu: {name}\n", offset=offset)


def type_text(vm, text):
    keys = {".": "dot", "-": "minus"}
    for character in text:
        press(vm, keys.get(character, character))


def exercise(vm, *, linux=False):
    prefix = "linux-menu" if linux else "menu"

    def screenshot(name):
        vm.shot(f"{prefix}-{name}")

    menu(vm, 0)
    screenshot("desktop")
    press(vm, "esc")

    application(vm, "files", lambda: menu(vm, 1, 1))
    menu(vm, 2)
    screenshot("files")
    press(vm, "down")
    press(vm, "ret")
    type_text(vm, "menu.txt")
    press(vm, "ret")
    application(vm, "editor", lambda: menu(vm, 2, 2))
    type_text(vm, "saved-from-top-menu")
    menu(vm, 1, 1)  # Closing a window with unsaved edits must leave it open.
    menu(vm, 2, 0)
    menu(vm, 4)
    screenshot("editor")
    press(vm, "esc")

    application(vm, "terminal", lambda: press(vm, "f1"))
    menu(vm, 2)
    screenshot("terminal")
    press(vm, "esc")
    application(vm, "editor", lambda: menu(vm, 1, 0))
    application(vm, "files", lambda: menu(vm, 1, 1))
    application(vm, "desktop", lambda: vm.click(1140, 504))

    application(vm, "settings", lambda: menu(vm, 0, 2))
    menu(vm, 2)
    screenshot("settings")
    press(vm, "down")
    press(vm, "down")
    press(vm, "ret")
    screenshot("display")

    menu(vm, 0, 0)
    type_text(vm, "explorer")
    application(vm, "explorer", lambda: press(vm, "ret"))
    menu(vm, 2)
    screenshot("explorer")
    press(vm, "esc")
    application(vm, "task-manager", lambda: press(vm, "f2"))
    menu(vm, 1)
    screenshot("task")
    press(vm, "esc")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=BUILD / "mayos.iso")
    parser.add_argument("--linux-kernel", type=Path)
    parser.add_argument("--linux-initramfs", type=Path)
    args = parser.parse_args()
    linux = args.linux_kernel is not None
    if linux != (args.linux_initramfs is not None):
        parser.error("--linux-kernel and --linux-initramfs must be supplied together")
    if linux:
        extra = ("-kernel", str(args.linux_kernel), "-initrd", str(args.linux_initramfs),
                 "-append", "console=tty0 console=ttyS0,115200 quiet rdinit=/init mayos.desktop=1 vga=0x315")
    else:
        extra = ()
    with tempfile.TemporaryDirectory(prefix="mayos-menu-") as folder:
        disk = Path(folder) / "disk.img"
        with disk.open("xb") as image:
            image.truncate(8 * 1024 * 1024)
        vm = BootVM(iso=None if linux else args.iso.resolve(), disk=disk,
                    tag="linux-dmenu" if linux else "desktop-menu",
                    qemu=shutil.which("qemu-system-x86_64"), memory=512, timeout=60,
                    extra_args=extra)
        try:
            vm.wait("MayOS: desktop ready")
            exercise(vm, linux=linux)
            if linux:
                vm.key("f1")
                offset = len(vm.text)
                vm.keyboard_command("cat Documents/menu.txt")
                vm.wait("\nsaved-from-top-menu\n", offset=offset)
        finally:
            vm.stop()
        if not linux:
            assert entries(disk)["menu.txt"][3] == "saved-from-top-menu"
    print("PASS: contextual menu switches on application focus, minimize, close and desktop click")
    print("PASS: Files creates a document and Editor saves it through the application menu")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Create and edit a MayFS document through the actual desktop inputs."""
from pathlib import Path
import shutil
import tempfile
import time

from desktop_menu_smoke import application, menu, press
from dual_boot_smoke import BUILD, BootVM


def type_text(vm, value):
    for ch in value:
        press(vm, "spc" if ch == " " else ch)


def edit(vm):
    menu(vm, 0, 0)
    type_text(vm, "editor")
    application(vm, "editor", lambda: press(vm, "ret"))
    press(vm, "ctrl-a")
    type_text(vm, "alpha beta gamma")
    press(vm, "home")
    for _ in range(6):
        press(vm, "right")
    for _ in range(4):
        press(vm, "shift-right")
    press(vm, "ctrl-x")
    type_text(vm, "delta")
    press(vm, "end")
    type_text(vm, " ")
    press(vm, "ctrl-v")
    press(vm, "ctrl-z")
    press(vm, "ctrl-y")
    vm.move(385, 237)
    vm.q("input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": True}}]})
    time.sleep(0.16)
    vm.move(425, 237)
    vm.q("input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": False}}]})
    time.sleep(0.16)
    press(vm, "ctrl-c")
    press(vm, "end")
    type_text(vm, " ")
    press(vm, "ctrl-v")
    menu(vm, 2, 0)
    vm.shot("editor-workflow")


def main():
    qemu = shutil.which("qemu-system-x86_64")
    with tempfile.TemporaryDirectory(prefix="mayos-editor-") as directory:
        disk = Path(directory) / "data.img"
        with disk.open("xb") as f:
            f.truncate(8 * 1024 * 1024)
        iso = BUILD / "mayos.iso"
        vm = BootVM(iso=iso, disk=disk, tag="editor-write", qemu=qemu, memory=512, timeout=45)
        try:
            vm.wait("MayOS: desktop ready")
            edit(vm)
        finally:
            vm.stop()
        vm = BootVM(iso=iso, disk=disk, tag="editor-read", qemu=qemu, memory=512, timeout=45)
        try:
            vm.wait("MayOS: MayFS mounted")
            vm.wait("MayOS: desktop ready")
            press(vm, "f1")
            vm.keyboard_command("cat Desktop/Notes.txt")
            vm.wait("\nalpha delta gamma beta delta\n")
        finally:
            vm.stop()
    print("PASS: editor caret, keyboard and mouse selection, cut/copy/paste, undo/redo and cold readback")


if __name__ == "__main__":
    main()

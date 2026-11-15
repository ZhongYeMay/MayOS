#!/usr/bin/env python3
"""Exercise native MayDOS ports on actual QEMU/MayKernel with real PS/2 input."""
from pathlib import Path
from PIL import Image, ImageChops
from smoke import VM, BUILD, entries

def changed(before, after):
    a, b = Image.open(BUILD / (before + ".png")).convert("RGB"), Image.open(BUILD / (after + ".png")).convert("RGB")
    assert a.size == b.size and ImageChops.difference(a, b).getbbox(), "GUI click did not redraw"

def main():
    disk = BUILD / "test-maydos.img"
    with disk.open("wb") as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = VM(disk, "test-maydos")
    try:
        vm.key("f1")
        vm.cmd("usebook")
        vm.wait("MayDOS: calc [expression]")
        vm.cmd("sysver")
        vm.wait("MayDOS 1.0.0 compatibility tools")
        vm.cmd("calc 1+2*3")
        vm.wait("Result:\n7\n")
        vm.cmd("calc (1.5+2.5)/2")
        vm.wait("Result:\n2\n")
        vm.cmd("calc 10/0")
        vm.wait("Division by zero")
        vm.cmd("calc")
        vm.shot("test-maydos-calc-before")
        vm.click(324, 321)
        vm.shot("test-maydos-calculator")
        changed("test-maydos-calc-before", "test-maydos-calculator")
        vm.key("f1")
        vm.cmd("minesweeper easy")
        vm.wait("MayDOS Minesweeper ready")
        vm.shot("test-maydos-minesweeper")
        vm.key("f1")
        vm.cmd("mine 0 0")
        vm.wait("Cell revealed")
        vm.cmd("flag 8 8")
        vm.wait("Flag updated")
        vm.cmd("add")
        vm.wait("no eligible new data")
        vm.cmd("write Documents/AddFiles/demo.txt HELLO-MAYDOS")
        vm.cmd("add demo.txt")
        vm.wait("MayDOS: imported into Documents/ThirdPartyFile")
        vm.cmd("list")
        vm.wait("\ndemo.txt\n")
        vm.cmd("apps")
        vm.shot("test-maydos-extensions")
        vm.key("f1")
        vm.cmd("del demo.txt")
        vm.wait("moved extension data to Trash")
        vm.cmd("write Documents/legacy.MayNote SGVsbG8K")
        vm.cmd("notepad Documents/legacy.MayNote")
        vm.shot("test-maydos-notepad")
        vm.key("ctrl-s")
        vm.key("f1")
        vm.cmd("cat Documents/legacy.MayNote")
        vm.wait("\nSGVsbG8K\n")
        data = entries(disk)
        assert data["legacy.MayNote"][3] == "SGVsbG8K"
        assert data["demo.txt"][2] == 3  # safely moved to Trash
        print("PASS: MayDOS GUI, calc CLI, mines, extensions, Base64 notepad and MayFS persistence", flush=True)
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

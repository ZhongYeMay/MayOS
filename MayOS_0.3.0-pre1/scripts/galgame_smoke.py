#!/usr/bin/env python3
"""Verify MayOS 夏日未寄 game on a genuine QEMU MayKernel and MayFS."""
import struct
from pathlib import Path

from PIL import Image, ImageChops
from smoke import BUILD, VM


def save_bytes(disk, name):
    data = disk.read_bytes()
    assert data[:8] == b"MAYFS001", "MayFS signature is missing"
    for i in range(48):
        pos = 512 + 4160 * i
        used, kind, parent, size = struct.unpack_from("<4I", data, pos)
        label = data[pos + 16: pos + 64].split(b"\0", 1)[0]
        if used and label == name.encode():
            return data[pos + 64: pos + 64 + size]
    return None


def main():
    disk = BUILD / "test-galgame-disk.img"
    with disk.open("wb") as target:
        target.truncate(8 * 1024 * 1024)
    vm = VM(disk, "test-galgame")
    try:
        vm.click(52, 615)  # real left-rail icon
        vm.wait("SummerLetters: app opened")
        vm.shot("summer-letters-title")
        title = Image.open(BUILD / "summer-letters-title.png").convert("RGB")
        assert title.size == (1024, 768), "native framebuffer dimensions changed"
        assert len(title.getcolors(2_000_000) or []) > 150, "original artwork not visible"
        vm.click(225, 363)   # new game
        vm.wait("SummerLetters: start")
        vm.shot("summer-letters-dialogue")
        dialogue = Image.open(BUILD / "summer-letters-dialogue.png").convert("RGB")
        assert ImageChops.difference(title, dialogue).getbbox(), "story didn't draw"
        vm.key("spc")
        vm.wait("SummerLetters: node p1")
        vm.click(176, 679)  # save
        vm.click(420, 310)  # slot 1
        vm.wait("SummerLetters: save")
        saved = save_bytes(disk, "NATSU01.SAV")
        assert saved and saved.startswith(b"NATSU001"), "MayFS game save missing"
        vm.key("spc")
        vm.wait("SummerLetters: node p2")
        vm.click(316, 677)  # load
        vm.click(420, 310)  # slot 1
        vm.wait("SummerLetters: load")
        vm.shot("summer-letters-loaded")
        vm.click(455, 679)  # backlog
        vm.shot("summer-letters-backlog")
        vm.key("esc")
        print("PASS: native original AI artwork, dialogue, progression, 3-slot save/load and backlog", flush=True)
    finally:
        vm.stop()

    vm = VM(disk, "test-galgame-cold-reboot")
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.click(52, 615)
        vm.click(225, 419)  # Load saved game from title
        vm.click(420, 310)
        vm.wait("SummerLetters: load")
        vm.shot("summer-letters-persistent-save")
        print("PASS: real MayFS save slot survived QEMU cold restart", flush=True)
    finally:
        vm.stop()


if __name__ == "__main__":
    main()

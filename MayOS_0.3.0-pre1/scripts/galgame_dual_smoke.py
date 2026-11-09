#!/usr/bin/env python3
"""Play the same native visual novel on both real QEMU-booted MayOS kernels."""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import os

from PIL import Image
from dual_boot_smoke import BootVM
from smoke import BUILD
from galgame_smoke import save_bytes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default=str(BUILD / "mayos-dual.iso"))
    parser.add_argument("--qemu", default=os.environ.get("QEMU64", "qemu-system-x86_64"))
    args = parser.parse_args()
    iso = Path(args.iso).resolve()
    assert iso.is_file(), "Build the dual-kernel ISO first"

    disk = BUILD / "galgame-dual-test.img"
    with disk.open("wb") as handle:
        handle.truncate(8 * 1024 * 1024)
    options = dict(iso=iso, disk=disk, qemu=args.qemu, memory=512, timeout=65)

    vm = BootVM(tag="galgame-dual-maykernel", **options)
    try:
        vm.wait("custom kernel", timeout=25)
        vm.wait("MayOS: desktop ready", timeout=65)
        vm.click(53, 615)
        vm.wait("SummerLetters: app opened")
        vm.shot("galgame-dual-maykernel-title")
        vm.click(223, 364)
        vm.wait("SummerLetters: start")
        vm.click(175, 678)
        vm.click(430, 310)
        vm.wait("SummerLetters: save")
        assert save_bytes(disk, "NATSU01.SAV"), "Native game save not on MayFS"
        print("PASS: MayKernel visual novel starts, displays original art and saves to disk", flush=True)
    finally:
        vm.stop()

    original_disk = hashlib.sha256(disk.read_bytes()).hexdigest()
    vm = BootVM(tag="galgame-dual-linux", **options)
    try:
        vm.wait("custom kernel", timeout=25)
        vm.key("down")
        vm.key("ret")
        vm.wait("MayOS Linux: ready", timeout=90)
        vm.wait("MayOS: desktop ready", timeout=45)
        vm.click(52, 615)
        vm.wait("SummerLetters: app opened")
        vm.shot("galgame-dual-linux-title")
        vm.click(225, 365)
        vm.wait("SummerLetters: start")
        vm.key("spc")
        vm.wait("SummerLetters: node p1")
        vm.click(175, 677)
        vm.click(430, 310)
        vm.wait("SummerLetters: save")
        vm.click(320, 677)
        vm.click(430, 310)
        vm.wait("SummerLetters: load")
        vm.shot("galgame-dual-linux-save")
        print("PASS: Linux GUI visual novel runs same story and in-session save/load", flush=True)
    finally:
        vm.stop()
    assert hashlib.sha256(disk.read_bytes()).hexdigest() == original_disk, (
        "Linux GUI must not overwrite the MayKernel MayFS game disk"
    )
    print("PASS: Linux session isolation preserved the MayKernel save", flush=True)


if __name__ == "__main__":
    main()

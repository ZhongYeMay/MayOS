#!/usr/bin/env python3
"""Boot the real safe-graphics GRUB entry under BIOS and OVMF.

Checks dual-kernel and standalone ISO menus on a 32-bit BIOS CPU and x64 UEFI,
including the actual guest framebuffer, keyboard and QEMU screen. Never use
QEMU -kernel or the default boot entry as a substitute for the fallback entry.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from PIL import Image
from dual_boot_smoke import BootVM, BUILD


ROOT = Path(__file__).resolve().parents[1]
OVMF_CODE = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
OVMF_VARS = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
QEMU64 = os.environ.get("QEMU64", "qemu-system-x86_64")


def checksum(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_case(kind: str, firmware: str) -> dict:
    iso = BUILD / ("mayos-dual.iso" if kind == "dual" else "mayos.iso")
    if not iso.is_file():
        raise FileNotFoundError(f"missing boot ISO: {iso}")
    tag = f"boot-safe-{kind}-{firmware}"
    disk = BUILD / f"{tag}.img"
    with disk.open("wb") as handle:
        handle.truncate(8 * 1024 * 1024)
    options = {}
    if firmware == "uefi64":
        if not (OVMF_CODE.is_file() and OVMF_VARS.is_file()):
            raise FileNotFoundError("OVMF firmware files are required for UEFI checks")
        variables = BUILD / f"{tag}-vars.fd"
        shutil.copyfile(OVMF_VARS, variables)
        options = {"ovmf_code": OVMF_CODE, "ovmf_vars": variables}
    vm = BootVM(
        iso=iso.resolve(), disk=disk, tag=tag, qemu=QEMU64,
        memory=512 if firmware == "uefi64" else 128,
        timeout=70, cpu="max" if firmware == "uefi64" else "pentium3",
        **options,
    )
    try:
        vm.wait("safe graphics", timeout=35)
        vm.key("home")   # select first entry before moving to the last
        vm.key("end")    # safe graphics is deliberately the final entry
        vm.key("ret")
        vm.wait("MayOS: entering custom x86 kernel")
        vm.wait("MayOS: desktop ready")
        modes = re.findall(r"MayOS: firmware framebuffer (\d+)x(\d+)x(\d+)", vm.text)
        if len(modes) != 1:
            raise AssertionError(f"expected one real framebuffer mode, got {modes!r}")
        width, height, bpp = map(int, modes[0])
        if width > 800 or height > 600 or width < 640 or height < 480 or bpp not in (16, 24, 32):
            raise AssertionError(f"safe graphics did not pick a supported low-res RGB mode: {modes[0]!r}")
        shot = vm.shot(tag)
        with Image.open(shot) as im:
            screen_size = im.size
            if screen_size != (width, height):
                raise AssertionError(f"QEMU display {screen_size} differs from guest {(width, height)}")
            if len(im.convert("RGB").getcolors(width * height) or []) < 16:
                raise AssertionError("guest framebuffer never drew the desktop")
        vm.key("f1")
        vm.keyboard_command("fastfetch")
        vm.wait("Kernel: MayKernel x86 (custom)")
        return {
            "case": tag, "firmware": firmware, "cpu": "pentium3" if firmware == "bios32" else "max",
            "iso": iso.name, "iso_sha256": checksum(iso),
            "framebuffer": f"{width}x{height}x{bpp}",
            "qemu_screenshot": shot.name, "desktop": True, "keyboard": True,
        }
    finally:
        vm.stop()


def main() -> None:
    cases = ((kind, firmware) for kind in ("custom", "dual") for firmware in ("bios32", "uefi64"))
    records = []
    for kind, firmware in cases:
        record = run_case(kind, firmware)
        records.append(record)
        print(f"PASS: {record['case']} {record['framebuffer']} real desktop and keyboard", flush=True)
    (BUILD / "boot-safe-graphics.json").write_text(json.dumps({
        "tested_at_utc": datetime.now(timezone.utc).isoformat(),
        "qemu": subprocess.check_output([QEMU64, "--version"], text=True).splitlines()[0],
        "tests": records,
    }, indent=2) + "\n")
    print("PASS: standalone and dual-kernel ISO low-resolution fallback on x86 BIOS / x64 UEFI", flush=True)


if __name__ == "__main__":
    main()

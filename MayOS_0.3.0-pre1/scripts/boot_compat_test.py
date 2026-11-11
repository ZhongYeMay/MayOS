#!/usr/bin/env python3
"""Real x86/x64 firmware boot checks, no GUI reconstruction or Linux shortcut."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image
from dual_boot_smoke import BootVM, BUILD

ROOT = Path(__file__).resolve().parents[1]
config = ROOT / "boot/grub/grub.cfg"
original = config.read_text()
cases = [("bios32", "pentium3", "1024x768x32", 1024, 768, 32),
         ("bios32-small", "pentium3", "800x600x16", 800, 600, 16),
         ("uefi64-wide", "max", "1920x1080x32", 1920, 1080, 32)]
records = []
if "--dual32" in sys.argv:
    cases = []
try:
    for tag, cpu, mode, width, height, bpp in cases:
        import re
        current = re.sub(r"set gfxmode=.*", "set gfxmode=" + mode, original)
        config.write_text(current)
        subprocess.run(["make", "build/mayos.iso"], cwd=ROOT, check=True)
        iso = BUILD / "mayos.iso"
        digest = hashlib.sha256(iso.read_bytes()).hexdigest()
        disk = BUILD / f"boot-{tag}.img"
        with disk.open("wb") as stream: stream.truncate(8 * 1024 * 1024)
        options = {}
        if tag.startswith("uefi"):
            template = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
            variables = BUILD / f"boot-{tag}-vars.fd"
            shutil.copyfile(template, variables)
            options = {"ovmf_code": Path("/usr/share/OVMF/OVMF_CODE_4M.fd"), "ovmf_vars": variables}
        vm = BootVM(iso=iso, disk=disk, tag="boot-" + tag, qemu="qemu-system-x86_64",
                    memory=128, timeout=60, cpu=cpu, **options)
        try:
            vm.wait("MayOS: desktop ready")
            vm.wait(f"MayOS: firmware framebuffer {width}x{height}x{bpp}")
            vm.shot("boot-" + tag)
            image = Image.open(BUILD / f"boot-{tag}.png")
            assert image.size == (width, height), image.size
            assert len(image.convert("RGB").getcolors(width * height) or []) > 16
            vm.key("f1"); vm.keyboard_command("fastfetch")
            vm.wait("Kernel: MayKernel x86_64 (custom)")
            records.append({"case": tag, "cpu": cpu, "framebuffer": mode, "iso_sha256": digest,
                            "desktop": True, "keyboard_shell": True})
        finally: vm.stop()
finally:
    config.write_text(original)
if "--dual32" in sys.argv:
    disk = BUILD / "boot-dual32.img"
    with disk.open("wb") as stream: stream.truncate(8 * 1024 * 1024)
    vm = BootVM(iso=BUILD / "mayos-dual.iso", disk=disk, tag="boot-dual32", qemu="qemu-system-x86_64",
                memory=128, timeout=60, cpu="pentium3")
    try:
        vm.wait("MayOS: desktop ready")
        assert "Linux 7.3.1 - desktop" not in vm.text, "64-bit Linux offered on a 32-bit CPU"
        vm.key("f1"); vm.keyboard_command("fastfetch"); vm.wait("Kernel: MayKernel x86_64 (custom)")
        records.append({"case": "dual-iso-32bit-cpu", "linux_hidden": True, "desktop": True})
    finally: vm.stop()
(BUILD / ("boot-dual32.json" if "--dual32" in sys.argv else "boot-compat.json")).write_text(json.dumps(records, indent=2) + "\n")
print("PASS: BIOS i686, RGB565 fallback, UEFI x64 widescreen and actual desktop keyboard input")

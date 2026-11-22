#!/usr/bin/env python3
"""Build a version-consistent official MayOS x86_64 BIOS/UEFI ISO.

MayKernel64 desktop, MayOS Live (RAM-only) and the installer-installed
MayKernel A slot are EXACTLY the same compiled MayOS 0.5.2 ELF bytes.

Linux64 is Linux kernel 7.3.1 with MayOS desktop from versions/0.5.2.
The offline installer reuses the SHA-pinned, QEMU-tested 0.5.2-pre2 Linux
installer driver/UI, but its real disk payload and visible version are
rebuilt from the 0.5.2 release ELF. No installer writer is weakened.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STABLE = ROOT / "versions/0.5.2"
sys.path.insert(0, str(STABLE / "scripts"))
from dual_iso import bootable_iso, checked_payloads, sha256  # noqa: E402
from retarget_installer_payload import create_payload  # noqa: E402

RELEASE = "0.5.2"
INSTALLER_SOURCE = "0.5.2-pre2"
INSTALLER_INPUT = "MayOS-0.5.2-pre2-Installer-x86_64.iso"
INSTALLER_INPUT_SHA256 = "c46ee45ded237f0c5823f94e498dad89700f7c54c0d80ba4137e5be3bf572a1a"
IMAGE_NAME = "MayOS-0.5.2-DualKernel-Live-Installer-x86_64.iso"


def extract(iso: Path, iso_path: str, output: Path):
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["xorriso", "-osirrox", "on", "-indev", str(iso),
         "-extract", iso_path, str(output)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
    if not output.is_file() or output.stat().st_size < 8192:
        raise ValueError(f"Missing ISO payload: {iso_path}")


def build(args):
    release_version = (STABLE / "VERSION").read_text().strip()
    if release_version != RELEASE:
        raise ValueError("Version source is not the required stable release")
    official_iso = args.kernel_iso.resolve(strict=True)
    installer_iso = args.installer_iso.resolve(strict=True)
    if installer_iso.name != INSTALLER_INPUT:
        raise ValueError("Installer input must have the exact pinned filename")
    if sha256(installer_iso) != INSTALLER_INPUT_SHA256:
        raise ValueError("Untrusted installer ISO SHA256; refusing to build")
    linux = STABLE / "build/linux"
    manifest = checked_payloads()
    if manifest["version"] != "7.3.1" or manifest["kernel_release"] != "7.3.1":
        raise ValueError("Linux64 kernel manifest version differs from 7.3.1")
    work = STABLE / "build/unified-official-iso"
    if work.exists():
        shutil.rmtree(work)
    boot = work / "boot"
    (boot / "grub").mkdir(parents=True)
    (boot / "linux").mkdir(parents=True)
    extract(official_iso, "/boot/mayos.elf", boot / "mayos.elf")
    if (b"MayOS Workstation 0.5.2 / MayShell" not in
            (boot / "mayos.elf").read_bytes()):
        raise ValueError("Live MayKernel advertises wrong version")
    subprocess.run(["grub-file", "--is-x86-multiboot", str(boot / "mayos.elf")],
                   check=True)
    extract(installer_iso, "/boot/installer-linux", boot / "installer-linux")
    old_installer = work / "old-installer.cpio.gz"
    extract(installer_iso, "/boot/installer.cpio.gz", old_installer)
    new_payload = create_payload(
        source_iso=official_iso, old_initramfs=old_installer,
        new_initramfs=boot / "installer.cpio.gz",
        expected_live_elf=boot / "mayos.elf")
    assert new_payload["version"] == RELEASE
    assert new_payload["installed_slot_sha256"] == sha256(boot / "mayos.elf")
    for filename in ("bzImage", "initramfs.cpio.gz", "manifest.json", "kernel.config"):
        shutil.copy2(linux / filename, boot / "linux" / filename)
    menu = (STABLE / "boot/grub/dual.cfg.in").read_text().replace(
        "@LINUX_VERSION@", manifest["kernel_release"])
    marker = "\nelse\nmenuentry"
    if menu.count(marker) != 1:
        raise ValueError("GRUB conditional layout changed")
    installer_entries = """
menuentry "Install MayOS 0.5.2 (BLANK DISK ONLY)" --id mayos-install {
    set gfxpayload=$gfxmode
    linux /boot/installer-linux console=tty0 quiet nomodeset mayos_gui
    initrd /boot/installer.cpio.gz
    boot
}
menuentry "Install/Recover MayOS 0.5.2 (serial console)" --id mayos-installer-serial {
    set gfxpayload=$gfxmode
    linux /boot/installer-linux console=ttyS0,115200 quiet
    initrd /boot/installer.cpio.gz
    boot
}
"""
    menu = menu.replace(marker, "\n" + installer_entries + marker)
    # The old RAM-only hardware trial is now a clearly named MayOS Live entry.
    if (menu.count('menuentry "MayOS Live 0.5.2') != 1 or
            menu.count("multiboot /boot/mayos.elf mayos.ram") != 1 or
            menu.count("multiboot /boot/mayos.elf\n") != 1 or
            menu.count("Install MayOS 0.5.2") != 1):
        raise ValueError("GRUB is missing the exact-version Live/desktop/install entries")
    if "serial --unit=0 --speed=115200" not in (
            STABLE / "boot/grub/menu.cfg").read_text():
        raise ValueError("GRUB menu lacks serial test control")
    (boot / "grub/grub.cfg").write_text(menu)
    shutil.copytree(STABLE / "boot/grub/themes", boot / "grub/themes")
    shutil.copy2(STABLE / "boot/grub/menu.cfg", boot / "grub/menu.cfg")
    shutil.copy2(STABLE / "assets/limine-mayos-brand.png",
                 boot / "grub/themes/mayos/mayos-brand.png")
    if (linux / "licenses").is_dir():
        shutil.copytree(linux / "licenses", work / "licenses")
    out = args.output.resolve()
    if out.name != IMAGE_NAME:
        raise ValueError("Official ISO filename must match RELEASE")
    out.parent.mkdir(parents=True, exist_ok=True)
    bootable_iso(work, out, argparse.Namespace(
        firmware="hybrid", grub_mkrescue="grub-mkrescue",
        grub_mkstandalone="grub-mkstandalone", grub_file="grub-file",
        grub_pc_dir=None, grub_efi_dir=None))
    if out.stat().st_size < 45 * 1024 * 1024:
        raise ValueError("Bootable ISO unexpectedly small")
    record = {
        "schema_version": 2,
        "release": RELEASE,
        "status": "official",
        "artifact": out.name,
        "sha256": sha256(out),
        "source_commit": args.source_commit,
        "firmware": ["BIOS", "UEFI x64"],
        "boot_modes": [
            {"id": "mayos", "name": "MayKernel64", "version": RELEASE,
             "kernel_sha256": sha256(boot / "mayos.elf"), "disk_writes_allowed": True},
            {"id": "mayos-live", "name": "MayOS Live", "version": RELEASE,
             "kernel_sha256": sha256(boot / "mayos.elf"),
             "boot_argument": "mayos.ram", "disk_writes_allowed": False},
            {"id": "linux", "name": "Linux64 MayOS desktop",
             "mayos_version": RELEASE, "linux_kernel_version": "7.3.1"},
            {"id": "mayos-install", "name": "MayOS offline installer",
             "version": RELEASE, "installed_version": RELEASE,
             "installed_kernel_sha256": new_payload["installed_slot_sha256"]},
        ],
        "installer": {
            "source_installer": INSTALLER_SOURCE,
            "source_installer_iso_sha256": INSTALLER_INPUT_SHA256,
            "installer_payload_sha256": new_payload["disk_payload_sha256"],
            "installer_initramfs_sha256": new_payload["installer_initramfs_sha256"],
            "installed_mayos_version": RELEASE,
        },
        "linux": {"version": "7.3.1",
                  "source_directory": "versions/0.5.2",
                  "bzImage_sha256": sha256(boot / "linux/bzImage"),
                  "initramfs_sha256": sha256(boot / "linux/initramfs.cpio.gz")},
        "physical_hardware_validation": "not established; QEMU verification only",
    }
    if len({record["boot_modes"][i]["kernel_sha256"] for i in (0, 1)} |
           {record["boot_modes"][3]["installed_kernel_sha256"]}) != 1:
        raise AssertionError("Live/MayKernel/installed kernels differ")
    (out.parent / (out.stem + ".manifest.json")).write_text(
        json.dumps(record, indent=2, ensure_ascii=False) + "\n")
    (out.parent / "SHA256SUMS.txt").write_text(
        f"{sha256(out)}  {out.name}\n")
    print(json.dumps(record, indent=2, ensure_ascii=False), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kernel-iso", type=Path, required=True)
    p.add_argument("--installer-iso", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--source-commit", required=True)
    args = p.parse_args()
    if (len(args.source_commit) != 40 or
            any(c not in "0123456789abcdef" for c in args.source_commit)):
        p.error("--source-commit must be a full lowercase commit SHA")
    build(args)


if __name__ == "__main__":
    main()

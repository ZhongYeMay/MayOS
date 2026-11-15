#!/usr/bin/env python3
"""Build a MayOS BIOS + x86_64 UEFI Limine ISO from verified payloads.

Limine consumes the existing Multiboot MayKernel hand-off, so this migration
does not weaken the independently tested x86_64 kernel bootstrap.
"""
from __future__ import annotations
import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def locate(root: Path, name: str) -> Path:
    candidates = list(root.rglob(name))
    if len(candidates) != 1:
        raise ValueError(f"expected exactly one {name} under {root}, found {len(candidates)}")
    return candidates[0]


def copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--limine-dir", type=Path, required=True,
                        help="Limine release directory containing BIOS/UEFI CD binaries")
    parser.add_argument("--limine", default="limine", help="Limine executable")
    parser.add_argument("--xorriso", default="xorriso")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, default=ROOT / "build/mayos.elf")
    parser.add_argument("--linux-kernel", type=Path)
    parser.add_argument("--initramfs", type=Path)
    args = parser.parse_args()
    if not args.kernel.is_file():
        raise FileNotFoundError(args.kernel)
    if bool(args.linux_kernel) != bool(args.initramfs):
        raise ValueError("--linux-kernel and --initramfs must be supplied together")

    with tempfile.TemporaryDirectory(prefix="mayos-limine-") as temporary:
        stage = Path(temporary) / "iso"
        copy(args.kernel, stage / "boot/mayos.elf")
        copy(ROOT / "boot/limine/limine.conf", stage / "limine.conf")
        copy(ROOT / "assets/limine-mayos-brand.png", stage / "boot/mayos-brand.png")
        if args.linux_kernel:
            copy(args.linux_kernel, stage / "boot/vmlinuz")
            copy(args.initramfs, stage / "boot/initramfs.cpio.gz")

        bios = locate(args.limine_dir, "limine-bios-cd.bin")
        uefi = locate(args.limine_dir, "limine-uefi-cd.bin")
        bootx64 = locate(args.limine_dir, "BOOTX64.EFI")
        copy(bios, stage / "boot/limine/limine-bios-cd.bin")
        copy(uefi, stage / "boot/limine/limine-uefi-cd.bin")
        copy(bootx64, stage / "EFI/BOOT/BOOTX64.EFI")

        args.output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([args.xorriso, "-as", "mkisofs", "-R", "-J",
                        "-b", "boot/limine/limine-bios-cd.bin", "-no-emul-boot",
                        "-boot-load-size", "4", "-boot-info-table",
                        "--efi-boot", "boot/limine/limine-uefi-cd.bin",
                        "-efi-boot-part", "--efi-boot-image", "--protective-msdos-label",
                        "-o", str(args.output), str(stage)], check=True)
        subprocess.run([args.limine, "bios-install", str(args.output)], check=True)
        print(f"Limine ISO: {args.output}")


if __name__ == "__main__":
    main()

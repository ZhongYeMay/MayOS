#!/usr/bin/env python3
"""Retarget a verified existing MayOS offline installer to an exact new kernel.

This transforms the *installer's real install payload*, NOT its menu label:
 - derives a GPT/UEFI MayOS disk with the official source install_mayos.py
 - replaces the 80 MiB disk image, corresponding SHA256, slot update ELF/hash
 - preserves the published 0.5.2-pre2 installer UI, writer and Linux drivers
 - updates the console's visible version and verifies the embedded slot ELF
The ISO and original initramfs must be pinned by the caller. No physical disks
are ever opened; all output is generated inside disposable directories.
"""
from __future__ import annotations
import gzip
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STABLE = ROOT / "versions/0.5.2"
sys.path.insert(0, str(STABLE / "scripts"))
from install_mayos import install, ESP_START, SECTOR  # noqa: E402

VERSION = "0.5.2"
OLD_HEADING = b"MayOS 0.5.1 | Installation and recovery"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cmd(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def create_payload(*, source_iso: Path, old_initramfs: Path, new_initramfs: Path,
                   expected_live_elf: Path, version: str = VERSION, install_image=install) -> dict:
    source_iso = source_iso.resolve(strict=True)
    old_initramfs = old_initramfs.resolve(strict=True)
    expected_live_elf = expected_live_elf.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="mayos-unified-installer-") as folder:
        work = Path(folder)
        tree = work / "cpio"
        tree.mkdir()
        with gzip.open(old_initramfs, "rb") as zipped:
            with (work / "original.cpio").open("wb") as plain:
                shutil.copyfileobj(zipped, plain, length=1024 * 1024)
        with (work / "original.cpio").open("rb") as plain:
            cmd("cpio", "-idm", "--quiet", "--no-absolute-filenames",
                cwd=tree, stdin=plain)
        data = tree / "installer"
        required = ("disk.img", "payload.sha256", "update.elf", "update.sha256",
                    "gui", "write-disk", "modules.sh")
        if any(not (data / part).is_file() for part in required):
            raise RuntimeError("Pinned installer initramfs missing mandatory components")
        disk = work / "new-installable-disk.img"
        install_image(source_iso, digest(source_iso), disk)
        slot = work / "installed-A.elf"
        cmd("mcopy", "-i", f"{disk}@@{ESP_START * SECTOR}",
            "::/slots/A.elf", str(slot))
        current_digest = digest(expected_live_elf)
        if digest(slot) != current_digest:
            raise AssertionError("FATAL: installed disk slot differs from live ISO MayKernel ELF")
        if f"MayOS Workstation {version} / MayShell".encode() not in slot.read_bytes():
            raise AssertionError(f"Compiled kernel does not advertise {version}")
        shutil.copyfile(disk, data / "disk.img")
        shutil.copyfile(slot, data / "update.elf")
        (data / "payload.sha256").write_text(
            f"{digest(data/'disk.img')}  /installer/disk.img\n")
        (data / "update.sha256").write_text(
            f"{digest(data/'update.elf')}  /installer/update.elf\n")
        original = (tree / "init").read_bytes()
        if original.count(OLD_HEADING) != 1:
            raise ValueError("Installer console version heading does not match pinned source")
        (tree / "init").write_bytes(original.replace(OLD_HEADING, f"MayOS {version} | Installation and recovery".encode()))
        (tree / "init").chmod(0o755)
        new_initramfs.parent.mkdir(parents=True, exist_ok=True)
        archive = work / "new.cpio"
        with archive.open("wb") as stream:
            files = subprocess.Popen(["find", ".", "-print0"], cwd=tree,
                                     stdout=subprocess.PIPE)
            assert files.stdout is not None
            result = subprocess.run(["cpio", "--null", "-o", "-H", "newc", "--quiet"],
                                    cwd=tree, stdin=files.stdout, stdout=stream)
            files.stdout.close()
            if files.wait() != 0 or result.returncode != 0:
                raise RuntimeError("Could not build safe newc installer archive")
        with archive.open("rb") as raw, new_initramfs.open("wb") as out:
            with gzip.GzipFile(filename="", mode="wb", fileobj=out,
                               compresslevel=6, mtime=0) as zipped:
                shutil.copyfileobj(raw, zipped, length=1024 * 1024)
        if not new_initramfs.stat().st_size:
            raise RuntimeError("Repacked installer initramfs is empty")
        return {
            "version": version,
            "installed_slot_sha256": current_digest,
            "disk_payload_sha256": digest(disk),
            "installer_initramfs_sha256": digest(new_initramfs),
            "source_iso_sha256": digest(source_iso),
            "previous_installer_initramfs_sha256": digest(old_initramfs),
        }

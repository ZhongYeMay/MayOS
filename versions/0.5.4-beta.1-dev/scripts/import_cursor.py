#!/usr/bin/env python3
"""Embed an original Windows cursor file in a new MayOS boot ISO."""
import argparse
import io
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile

from linux import cpio_member

MAX_BYTES = 256 * 1024


def import_cursor(source, cursor, output):
    if output.exists() or output.is_symlink():
        raise ValueError("Choose a new ISO filename; the original ISO is preserved")
    if not source.is_file() or not cursor.is_file():
        raise ValueError("Choose a MayOS ISO and a regular cursor file")
    if not 12 <= cursor.stat().st_size <= MAX_BYTES:
        raise ValueError("The cursor must fit within 256 KiB")
    data = cursor.read_bytes()
    if not 12 <= len(data) <= MAX_BYTES:
        raise ValueError("The cursor changed or exceeds 256 KiB")
    suffix = cursor.suffix.lower()
    if not (suffix == ".cur" and data[:4] == b"\x00\x00\x02\x00" or
            suffix == ".ani" and data[:4] == b"RIFF" and data[8:12] == b"ACON"):
        raise ValueError("Expected a Windows .cur or .ani file")
    with tempfile.TemporaryDirectory(prefix=".mayos-cursor-", dir=output.parent) as tmp:
        root = Path(tmp)
        config = root / "grub.cfg"
        embedded = root / "mayos-pointer.bin"
        embedded.write_bytes(data)
        subprocess.run(["xorriso", "-abort_on", "FAILURE", "-osirrox", "on", "-indev", str(source),
                        "-extract", "/boot/grub/grub.cfg", str(config)], check=True)
        text = config.read_text()
        if "/boot/mayos-pointer" in text:
            raise ValueError("Use the original MayOS ISO as the input")
        text, n = re.subn(r"(?m)^( +)multiboot /boot/mayos\.elf[^\n]*$",
                          lambda m: m[0] + "\n" + m[1] + "module /boot/mayos-pointer.bin mayos-pointer",
                          text)
        if not n:
            raise ValueError("This ISO does not contain a supported MayKernel boot entry")
        linux_archive = "/boot/mayos-pointer.cpio"
        text, linux_entries = re.subn(
            r"(?m)^( +initrd /boot/linux/initramfs\.cpio\.gz)\s*$",
            lambda m: m[1] + " " + linux_archive, text)
        config.chmod(0o644)
        config.write_text(text)
        staged = root / "output.iso"
        command = ["xorriso", "-abort_on", "FAILURE", "-indev", str(source), "-outdev", str(staged),
                   "-map", str(config), "/boot/grub/grub.cfg", "-map", str(embedded),
                   "/boot/mayos-pointer.bin"]
        if linux_entries:
            archive = io.BytesIO()
            cpio_member(archive, 1, "mayos-pointer.bin", stat.S_IFREG | 0o444, data)
            cpio_member(archive, 2, "TRAILER!!!", 0, b"")
            archive_path = root / "mayos-pointer.cpio"
            archive_path.write_bytes(archive.getvalue())
            command += ["-map", str(archive_path), linux_archive]
        subprocess.run(command + ["-boot_image", "any", "replay", "-commit", "-end"], check=True)
        os.link(staged, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("iso", "cursor", "output"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    try:
        import_cursor(args.iso.resolve(), args.cursor.resolve(), args.output.absolute())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()

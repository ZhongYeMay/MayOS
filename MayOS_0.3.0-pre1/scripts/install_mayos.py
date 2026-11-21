#!/usr/bin/env python3
"""Install a checked MayKernel ISO into a fresh GPT/UEFI virtual hard disk."""
import argparse
import os
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
import tempfile

from live_upgrade import DIGEST, digest, regular


SECTOR = 512
ESP_START = 2048
ESP_BLOCKS = 131072
DATA_START = ESP_START + ESP_BLOCKS
DATA_BLOCKS = 16384
DISK_BYTES = 80 * 1024 * 1024
SNAPSHOT_BYTES = 512 * SECTOR
FILE_TABLE_BYTES = 48 * 4160
MAYFS = "4D41594F-532D-4653-8C4D-415946533031"
ESP = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"


def command(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def legacy_snapshots(path):
    # Copy both slots intact after checking that at least one MayFS file table
    # is readable. The guest independently validates records and generations.
    with regular(path).open("rb") as source:
        image = source.read(2 * SNAPSHOT_BYTES)
    if len(image) < SNAPSHOT_BYTES:
        raise ValueError("The legacy MayFS image is too short")
    for offset in (0, SNAPSHOT_BYTES):
        snapshot = image[offset:offset + SNAPSHOT_BYTES]
        if not (len(snapshot) == SNAPSHOT_BYTES and snapshot.startswith(b"MAYFS001")):
            continue
        checksum = 2166136261
        for byte in snapshot[SECTOR:SECTOR + FILE_TABLE_BYTES]:
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        if checksum == struct.unpack_from("<I", snapshot, 8)[0]:
            return image.ljust(2 * SNAPSHOT_BYTES, b"\0")
    raise ValueError("The source has no valid legacy MayFS snapshot")


def install(iso, expected, output, imported=None):
    if not DIGEST.fullmatch(expected):
        raise ValueError("A trusted SHA-256 digest is required")
    if output.exists() or output.is_symlink():
        raise ValueError("Choose a new image filename. Existing files are protected")
    data = legacy_snapshots(imported) if imported else None
    # Construct and validate in a private directory on the destination
    # filesystem, then publish by exclusive hard link. No raw device access.
    with tempfile.TemporaryDirectory(prefix=".mayos-install-", dir=output.parent) as directory:
        root = Path(directory)
        checked_iso = root / "checked.iso"
        shutil.copyfile(regular(iso), checked_iso)
        if digest(checked_iso) != expected:
            raise ValueError("ISO SHA-256 does not match")
        kernel = root / "mayos.elf"
        command("xorriso", "-osirrox", "on", "-indev", str(checked_iso),
                "-extract", "/boot/mayos.elf", str(kernel))
        command("grub-file", "--is-x86-multiboot", str(kernel))
        if b"MayOS: MayFS GPT partition selected" not in kernel.read_bytes():
            raise ValueError("This ISO predates MayFS partition support")

        disk = root / "disk.img"
        with disk.open("xb") as handle:
            handle.truncate(DISK_BYTES)
        layout = (f"label: gpt\nunit: sectors\n\n"
                  f"start={ESP_START}, size={ESP_BLOCKS}, type={ESP}, name=MayOS-BOOT\n"
                  f"start={DATA_START}, size={DATA_BLOCKS}, type={MAYFS}, name=MayFS\n")
        command("sfdisk", str(disk), input=layout, text=True)
        command("sfdisk", "--verify", str(disk))

        serial = secrets.token_hex(4).upper()
        volume_id = f"{serial[:4]}-{serial[4:]}"
        fat = root / "esp.img"
        loader = root / "BOOTX64.EFI"
        config = root / "grub.cfg"
        config.write_text(
            f"search --no-floppy --fs-uuid --set=root {volume_id}\n"
            "set timeout=0\n"
            "menuentry 'MayOS installed' {\n"
            "  insmod all_video\n"
            "  loadfont (memdisk)/boot/grub/fonts/unicode.pf2\n"
            "  set gfxmode=1280x720x32,1024x768x32,auto\n"
            "  terminal_output gfxterm\n"
            "  multiboot /slots/A.elf\n"
            "  set gfxpayload=1280x720x32,1024x768x32,auto\n"
            "  boot\n"
            "}\n")
        command("grub-mkstandalone", "-O", "x86_64-efi", "--fonts=unicode",
                "--locales=", "--themes=", "--compress=xz", "-o", str(loader),
                f"boot/grub/grub.cfg={config}")
        command("grub-file", "--is-x86_64-efi", str(loader))
        command("mformat", "-C", "-F", "-T", str(ESP_BLOCKS), "-N", serial,
                "-v", "MAYOSBOOT", "-i", str(fat), "::")
        command("mmd", "-i", str(fat), "::/EFI", "::/EFI/BOOT", "::/slots")
        command("mcopy", "-i", str(fat), str(loader), "::/EFI/BOOT/BOOTX64.EFI")
        command("mcopy", "-i", str(fat), str(kernel), "::/slots/A.elf")
        with disk.open("r+b") as handle, fat.open("rb") as boot:
            handle.seek(ESP_START * SECTOR)
            shutil.copyfileobj(boot, handle)
            if data:
                handle.seek(DATA_START * SECTOR)
                handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.link(disk, output)
        fd = os.open(output.parent, os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    print(f"Created the MayOS UEFI installation image: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--import-mayfs", type=Path)
    args = parser.parse_args()
    try:
        install(args.iso, args.sha256, args.output.absolute(), args.import_mayfs)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Install failed: {error}\n")


if __name__ == "__main__":
    main()

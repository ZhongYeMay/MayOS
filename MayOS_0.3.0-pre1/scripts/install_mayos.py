#!/usr/bin/env python3
"""Install a checked MayKernel ISO into a fresh GPT/UEFI virtual hard disk."""
import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
import tempfile
import time

from live_upgrade import DIGEST, digest, regular, version_order


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


def kernel_from_iso(iso, expected, root):
    if not DIGEST.fullmatch(expected):
        raise ValueError("A trusted SHA-256 digest is required")
    checked = root / "checked.iso"
    shutil.copyfile(regular(iso), checked)
    if digest(checked) != expected:
        raise ValueError("ISO SHA-256 does not match")
    kernel = root / "mayos.elf"
    command("xorriso", "-osirrox", "on", "-indev", str(checked),
            "-extract", "/boot/mayos.elf", str(kernel))
    command("grub-file", "--is-x86-multiboot", str(kernel))
    if b"MayOS: MayFS GPT partition selected" not in kernel.read_bytes():
        raise ValueError("This ISO predates MayFS partition support")
    return kernel


def new_destination(output):
    if output.exists() or output.is_symlink():
        raise ValueError("Choose a new image filename. Existing files are protected")


def publish(disk, output):
    with disk.open("rb") as handle:
        os.fsync(handle.fileno())
    os.link(disk, output)
    fd = os.open(output.parent, os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def boot_config(volume, slots):
    text = (f"search --no-floppy --fs-uuid --set=root {volume}\n"
            "set default=0\nset timeout=2\n"
            "set gfxmode=1280x720x32,1024x768x32,auto\n"
            "insmod all_video\n"
            "loadfont (memdisk)/boot/grub/fonts/unicode.pf2\n"
            "terminal_output gfxterm\n")
    for slot in slots:
        if slot not in ("A", "B"):
            raise ValueError("Invalid boot slot")
        text += (f"menuentry 'MayOS slot {slot}' {{\n"
                 f"  multiboot /slots/{slot}.elf\n"
                 "  set gfxpayload=$gfxmode\n  boot\n}\n")
    return text


def set_boot_files(root, fat, volume, slots):
    config, state = root / "mayos-grub.cfg", root / "mayos-slot.json"
    config.write_text(boot_config(volume, slots))
    state.write_text(json.dumps({"volume": volume, "active": slots[0]}))
    command("mcopy", "-o", "-i", str(fat), str(config), str(state), "::/")


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
    new_destination(output)
    data = legacy_snapshots(imported) if imported else None
    # Construct and validate in a private directory on the destination
    # filesystem, then publish by exclusive hard link. No raw device access.
    with tempfile.TemporaryDirectory(prefix=".mayos-install-", dir=output.parent) as directory:
        root = Path(directory)
        kernel = kernel_from_iso(iso, expected, root)

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
            "configfile /mayos-grub.cfg\n")
        command("grub-mkstandalone", "-O", "x86_64-efi", "--fonts=unicode",
                "--locales=", "--themes=", "--compress=xz", "-o", str(loader),
                f"boot/grub/grub.cfg={config}")
        command("grub-file", "--is-x86_64-efi", str(loader))
        command("mformat", "-C", "-F", "-T", str(ESP_BLOCKS), "-N", "0x" + serial,
                "-v", "MAYOSBOOT", "-i", str(fat), "::")
        with fat.open("rb") as handle:
            handle.seek(67)  # FAT32 extended BPB volume serial
            if int.from_bytes(handle.read(4), "little") != int(serial, 16):
                raise ValueError("EFI filesystem serial does not match its GRUB boot target")
        command("mmd", "-i", str(fat), "::/EFI", "::/EFI/BOOT", "::/slots")
        command("mcopy", "-i", str(fat), str(loader), "::/EFI/BOOT/BOOTX64.EFI")
        command("mcopy", "-i", str(fat), str(kernel), "::/slots/A.elf")
        set_boot_files(root, fat, volume_id, ["A"])
        with disk.open("r+b") as handle, fat.open("rb") as boot:
            handle.seek(ESP_START * SECTOR)
            shutil.copyfileobj(boot, handle)
            if data:
                handle.seek(DATA_START * SECTOR)
                handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        publish(disk, output)
    print(f"Created the MayOS UEFI installation image: {output}")


def trial_boot(root, disk, version, ovmf, vars_template):
    log, vars_path = root / "serial.log", root / "vars.fd"
    shutil.copyfile(regular(vars_template), vars_path)
    drive = lambda path: str(path).replace(",", ",,")
    args = ["qemu-system-x86_64", "-machine", "pc", "-accel", "tcg",
            "-cpu", "max", "-m", "256", "-display", "none", "-monitor", "none",
            "-nic", "none", "-vga", "std", "-serial", f"file:{log}", "-no-reboot",
            "-drive", f"if=pflash,format=raw,readonly=on,file={drive(regular(ovmf))}",
            "-drive", f"if=pflash,format=raw,file={drive(vars_path)}",
            "-drive", f"format=raw,if=ide,index=0,snapshot=on,file={drive(disk)}",
            "-boot", "c"]
    with subprocess.Popen(args) as vm:
        try:
            deadline = time.monotonic() + 40
            required = {"MayOS: desktop ready", "MayOS: MayFS mounted",
                        f"MayOS Workstation {version} / MayShell"}
            while time.monotonic() < deadline and vm.poll() is None:
                if log.is_file() and required <= set(log.read_text(errors="replace").splitlines()):
                    return
                time.sleep(.1)
            tail = log.read_text(errors="replace")[-3000:] if log.is_file() else ""
            raise ValueError(f"Installed-disk trial failed\n{tail}")
        finally:
            if vm.poll() is None:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait()


def upgrade(source, iso, expected, output, version, ovmf, vars_template):
    version_order(version)
    new_destination(output)
    source = regular(source)
    if source.stat().st_size != DISK_BYTES:
        raise ValueError("Choose an image created by this installer")
    with tempfile.TemporaryDirectory(prefix=".mayos-upgrade-", dir=output.parent) as directory:
        root = Path(directory)
        kernel = kernel_from_iso(iso, expected, root)
        disk = root / "upgraded.img"
        shutil.copyfile(source, disk)
        command("sfdisk", "--verify", str(disk))
        raw = command("sfdisk", "--json", str(disk), capture_output=True, text=True)
        layout = json.loads(raw.stdout)["partitiontable"]
        actual = [(p["start"], p["size"], p["type"].upper()) for p in layout["partitions"]]
        if (layout["label"], layout["sectorsize"], actual) != (
                "gpt", SECTOR, [(ESP_START, ESP_BLOCKS, ESP), (DATA_START, DATA_BLOCKS, MAYFS)]):
            raise ValueError("Unexpected partition layout")
        fat = f"{disk}@@{ESP_START * SECTOR}"
        state = root / "read-slot.json"
        command("mcopy", "-i", fat, "::/mayos-slot.json", str(state))
        metadata = json.loads(state.read_text())
        old, volume = metadata["active"], metadata["volume"]
        if old not in ("A", "B") or not isinstance(volume, str) or len(volume) != 9 or any(
                ch not in "0123456789ABCDEF-" for ch in volume):
            raise ValueError("Invalid MayOS boot metadata")
        new = "B" if old == "A" else "A"
        command("mcopy", "-o", "-i", fat, str(kernel), f"::/slots/{new}.elf")
        set_boot_files(root, fat, volume, [new, old])
        trial_boot(root, disk, version, ovmf, vars_template)
        publish(disk, output)
    print(f"Created trial-verified MayOS {version} image: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--import-mayfs", type=Path)
    parser.add_argument("--upgrade-from", type=Path)
    parser.add_argument("--version")
    parser.add_argument("--ovmf", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    try:
        if args.upgrade_from:
            if not args.version or args.import_mayfs:
                parser.error("Upgrade requires --version and uses the existing MayFS partition")
            upgrade(args.upgrade_from, args.iso, args.sha256, args.output.absolute(),
                    args.version, args.ovmf, args.ovmf_vars)
        else:
            install(args.iso, args.sha256, args.output.absolute(), args.import_mayfs)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Install failed: {error}\n")


if __name__ == "__main__":
    main()

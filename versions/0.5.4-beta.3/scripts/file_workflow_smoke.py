#!/usr/bin/env python3
"""Migrate a 4Kn MayFS installation, import a binary file, and copy a folder."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from desktop_menu_smoke import application, menu, press
from dual_boot_smoke import BUILD
from install_mayos import install, SECTOR_4K
from installed_storage_smoke import complete_setup, gpt_layout
from phase1_powercut_smoke import nvme_vm, sha


def command(vm, line, expected):
    offset = len(vm.text)
    vm.keyboard_command(line)
    vm.wait(expected, offset=offset)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--legacy-iso", required=True, type=Path)
    parser.add_argument("--pack-iso", required=True, type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--ovmf-code", required=True, type=Path)
    parser.add_argument("--ovmf-vars", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mayos-files-") as work:
        root = Path(work)
        disk = root / "installed.img"
        install(args.legacy_iso, sha(args.legacy_iso), disk, sector=SECTOR_4K)
        layout = gpt_layout(disk, SECTOR_4K, verify=True)
        start = layout["partitions"][1]["start"] * SECTOR_4K

        def boot(tag, iso=None):
            return nvme_vm(root, disk, iso, tag, args.ovmf_code, args.ovmf_vars)

        vm = boot("files-legacy")
        try:
            vm.wait("MayOS: first boot setup ready")
            complete_setup(vm)
            vm.wait("MayOS: desktop ready")
            press(vm, "f1")
            command(vm, "write Documents/legacy.txt survives-migration", "\nOK\n")
        finally:
            vm.stop()
        with disk.open("rb") as stream:
            stream.seek(start)
            legacy = stream.read(512 * 1024)
        assert legacy.startswith(b"MAYFS001")

        # Replace only the EFI executable on a disposable image. The old MayFS
        # slots are still intact and are loaded by the first new kernel boot.
        esp = f"{disk}@@{1024 * 1024}"
        subprocess.run(["mcopy", "-o", "-i", esp, str(BUILD / "mayos.elf"), "::/slots/A.elf"], check=True)
        vm = boot("files-import", args.pack_iso)
        try:
            vm.wait("MayOS: desktop ready")
            application(vm, "files", lambda: menu(vm, 1, 1))
            menu(vm, 2, 2)
            vm.wait("MayOS: file pack imported and saved")
            press(vm, "f1")
            command(vm, f"cp Documents/{args.binary.name} Desktop", "Copied")
            command(vm, "mkdir Documents/Project", "\nOK\n")
            command(vm, "mkdir Documents/Project/Nested", "\nOK\n")
            command(vm, "write Documents/Project/Nested/keep.txt nested", "\nOK\n")
            command(vm, "cp Documents/Project Desktop", "Copied")
            vm.shot("file-pack-workflow")
        finally:
            vm.stop()

        vm = boot("files-cold")
        try:
            vm.wait("MayOS: MayFS mounted")
            vm.wait("MayOS: desktop ready")
            press(vm, "f1")
            command(vm, "cat Documents/legacy.txt", "\nsurvives-migration\n")
            command(vm, "cat Desktop/Project/Nested/keep.txt", "\nnested\n")
        finally:
            vm.stop()

        expected = args.binary.read_bytes()
        assert len(expected) > 4095 and b"\0" in expected
        with disk.open("rb") as stream:
            stream.seek(start)
            assert stream.read(512 * 1024) == legacy
            for slot in (512 * 1024, 4 * 1024 * 1024):
                stream.seek(start + slot)
                snapshot = stream.read(3584 * 1024)
                assert snapshot.startswith(b"MAYFS002")
                found = set()
                for i in range(96):
                    record = 512 + i * (64 + 32768)
                    used, kind, parent, size = struct.unpack_from("<4I", snapshot, record)
                    name = snapshot[record + 16:record + 64].split(b"\0", 1)[0]
                    if used and not kind and name == args.binary.name.encode():
                        assert snapshot[record + 64:record + 64 + size] == expected
                        found.add(parent)
                assert found == {1, 2}
    print("PASS: pre19 4Kn disk migrated with old snapshots intact, 20 KB binary imported/copied, nested folder persisted")


if __name__ == "__main__":
    main()

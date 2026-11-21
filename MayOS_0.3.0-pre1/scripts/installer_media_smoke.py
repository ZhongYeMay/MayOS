#!/usr/bin/env python3
"""Drive the actual installer console, then boot the disk without the ISO."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from dual_boot_smoke import BUILD, BootVM
from install_mayos import DATA_BLOCKS, DATA_START, DISK_BYTES, SECTOR
from live_upgrade import digest


def main():
    iso = BUILD / "mayos-installer.iso"
    upgrade_iso = BUILD / "mayos-installer-upgrade-test.iso"
    code = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    variables = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        disk = root / "target.img"
        target_size = DISK_BYTES + 16 * 1024 * 1024
        with disk.open("xb") as handle:
            handle.truncate(target_size)

        def start(tag, media, *, uefi=True):
            firmware = {}
            if uefi:
                vars_path = root / f"{tag}.fd"
                shutil.copyfile(variables, vars_path)
                firmware = {"ovmf_code": code, "ovmf_vars": vars_path}
            return BootVM(iso=media, disk=disk, tag=tag, qemu="qemu-system-x86_64",
                          memory=512, timeout=90, **firmware)

        def console(vm, *, installed=False):
            vm.wait("Install MayOS (serial console)")
            vm.send_serial("\x1b[B\r")
            vm.wait("Enter the target path")
            vm.send_serial("/dev/sda\n")
            vm.wait("MayOS installed disk detected." if installed else
                    "To continue, type INSTALL /dev/sda")

        def media_action(tag, action, expected):
            vm = start(tag, upgrade_iso)
            try:
                console(vm, installed=True)
                vm.send_serial(f"{action} /dev/sda\n")
                vm.wait(expected)
            finally:
                vm.stop()

        def boot_version(tag, version, *, seed=False):
            vm = start(tag, None)
            try:
                vm.wait("MayOS: MayFS GPT partition selected")
                vm.wait(f"MayOS Workstation {version} / MayShell")
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                if seed:
                    vm.keyboard_command("write keep.txt installer-preserved")
                    vm.wait("\nOK\n")
                else:
                    vm.keyboard_command("cat keep.txt")
                    vm.wait("\ninstaller-preserved\n")
            finally:
                vm.stop()

        def data_digest():
            import hashlib
            with disk.open("rb") as handle:
                handle.seek(DATA_START * SECTOR)
                return hashlib.sha256(handle.read(DATA_BLOCKS * SECTOR)).hexdigest()

        before = digest(disk)
        vm = start("installer-media-cancel-bios", iso, uefi=False)
        try:
            console(vm)
            vm.send_serial("cancel\n")
            vm.wait("Confirmation cancelled.")
        finally:
            vm.stop()
        assert digest(disk) == before

        with disk.open("r+b") as handle:
            handle.seek(target_size - 1024 * 1024)
            handle.write(b"existing user data")
        before = digest(disk)
        vm = start("installer-media-nonblank", iso)
        try:
            console(vm)
            vm.send_serial("INSTALL /dev/sda\n")
            vm.wait("Target contains data. No sectors were written.")
        finally:
            vm.stop()
        assert digest(disk) == before
        print("PASS: BIOS cancellation and UEFI nonblank detection leave every byte intact")

        with disk.open("wb") as handle:
            handle.truncate(target_size)
        vm = start("installer-media-install", iso)
        try:
            console(vm)
            vm.send_serial("INSTALL /dev/sda\n")
            vm.wait("MayOS installation complete.")
        finally:
            vm.stop()
        subprocess.run(["sfdisk", "--verify", str(disk)], check=True)
        boot_version("installer-media-hard-disk", "0.5.1-pre1", seed=True)
        print("PASS: interactive ISO installation, readback verification and ISO-free hard-disk boot")

        original_data = data_digest()
        media_action("installer-media-stage", "UPGRADE", "Trial queued.")
        media_action("installer-media-premature", "CONFIRM", "has not completed a trial attempt")
        boot_version("installer-media-trial", "0.5.1-pre2")
        boot_version("installer-media-fallback", "0.5.1-pre1")
        assert data_digest() == original_data
        print("PASS: a trial boots exactly once and the next boot returns to the confirmed slot")

        media_action("installer-media-confirm", "CONFIRM", "New MayOS slot confirmed.")
        boot_version("installer-media-confirmed", "0.5.1-pre2")
        assert data_digest() == original_data
        print("PASS: confirmed media upgrade boots the new slot with no changes to MayFS")


if __name__ == "__main__":
    main()

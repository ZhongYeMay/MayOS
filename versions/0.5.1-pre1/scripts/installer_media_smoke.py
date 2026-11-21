#!/usr/bin/env python3
"""Drive the actual installer console, then boot the disk without the ISO."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from dual_boot_smoke import BUILD, BootVM
from install_mayos import DISK_BYTES
from live_upgrade import digest


def main():
    iso = BUILD / "mayos-installer.iso"
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

        def console(vm):
            vm.wait("Install MayOS (serial console)")
            vm.send_serial("\x1b[B\r")
            vm.wait("Enter the target path")
            vm.send_serial("/dev/sda\n")
            vm.wait("To continue, type INSTALL /dev/sda")

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
        vm = start("installer-media-hard-disk", None)
        try:
            vm.wait("MayOS: MayFS GPT partition selected")
            vm.wait("MayOS Workstation 0.5.1-pre1 / MayShell")
            vm.wait("MayOS: desktop ready")
        finally:
            vm.stop()
        print("PASS: interactive ISO installation, readback verification and ISO-free hard-disk boot")


if __name__ == "__main__":
    main()

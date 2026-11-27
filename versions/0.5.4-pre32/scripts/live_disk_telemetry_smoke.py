#!/usr/bin/env python3
"""Check that Live disk discovery reports hardware without changing the disk."""

import argparse
import hashlib
from pathlib import Path
import tempfile
import time

from dual_boot_smoke import BootVM
from procfs_smoke import command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mayos-live-disk-") as directory:
        disk = Path(directory) / "data.img"
        with disk.open("wb") as image:
            image.write(b"Keep this non-MayFS data intact.")
            image.truncate(8 * 1024 * 1024)
        digest = hashlib.sha256(disk.read_bytes()).digest()
        for backend in ("none", "ata", "ahci", "nvme"):
            device = {
                "ahci": "ide-hd,bus=ide.0,drive=live-test",
                "nvme": "nvme,drive=live-test,serial=mayos-live-test",
            }.get(backend)
            extra = ("-drive", f"file={disk},format=raw,if=none,id=live-test",
                     "-device", device) if device else ()
            vm = BootVM(
                iso=args.iso.resolve(), disk=disk if backend == "ata" else None,
                tag=f"live-disk-{backend}", qemu=args.qemu, memory=512, timeout=45,
                machine="q35" if device else "pc", extra_args=extra,
            )
            try:
                vm.wait("MayKernel64 display modes")
                vm.key("down")
                vm.key("ret")
                vm.wait("session filesystem runs in RAM; disk writes disabled")
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                command(vm, "cat /proc/mounts", "mayfs-ram / mayfs")
                offset = len(vm.text)
                vm.keyboard_command("cat /proc/diskstats")
                command(vm, "write /live-only.txt ram-only-data", "\nOK (RAM only; disk sync unavailable)\n")
                command(vm, "cat /live-only.txt", "\nram-only-data\n")
                stats = vm.text[offset:]
                if backend == "none":
                    assert "disk0 " not in stats, stats
                else:
                    assert "disk0 0 0 0 0 0 0 0 0 0 0 0" in stats, stats
                vm.key("f2")
                vm.wait("MayOS: Task Manager opened")
                vm.key("down")
                vm.key("down")
                vm.wait("MayOS Task Manager: showing Disk")
                time.sleep(3.6)
                vm.shot(f"live-disk-{backend}-task-manager")
            finally:
                vm.stop()
            assert hashlib.sha256(disk.read_bytes()).digest() == digest
            print(f"PASS: {backend} Live disk stats, RAM-only MayFS and unchanged disk", flush=True)


if __name__ == "__main__":
    main()

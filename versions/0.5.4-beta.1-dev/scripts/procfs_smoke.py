#!/usr/bin/env python3
"""Verify native procfs using the real MayShell and framebuffer."""
import argparse
from pathlib import Path
import re
import tempfile
import time

from dual_boot_smoke import BootVM
from smoke import entries


def command(vm, text, expected):
    offset = len(vm.text)
    vm.keyboard_command(text)
    return vm.wait(expected, offset=offset)[offset:]


def read_fields(vm, path, pattern):
    offset = len(vm.text)
    vm.keyboard_command(f"cat {path}")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        match = re.search(pattern, vm.text[offset:], re.MULTILINE)
        if match:
            return match.group(1).split()
        time.sleep(.05)
    raise AssertionError(f"missing expected fields in {path}")


def uptime(vm):
    return float(read_fields(vm, "/proc/uptime", r"^(\d+\.\d{2}) \d+\.\d{2}$")[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--legacy-iso", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mayos-proc-test-") as directory:
        disk = Path(directory) / "data.img"
        with disk.open("wb") as image:
            image.truncate(8 * 1024 * 1024)
        options = dict(disk=disk, qemu="qemu-system-x86_64", memory=512, timeout=35)
        vm = BootVM(iso=args.iso.resolve(), tag="procfs-native", **options)
        try:
            vm.wait("Rust procfs mounted read-only at /proc")
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            command(vm, "ls /", "[dir]  proc")
            command(vm, "cd /proc//sys/kernel", "cd /proc//sys/kernel")
            command(vm, "pwd", "\n/proc/sys/kernel\n")
            command(vm, "cat ostype", "\nMayKernel\n")
            command(vm, "cat /proc/cpuinfo", "model name")
            command(vm, "cat /proc/meminfo", "MemAvailable:")
            command(vm, "cat /proc/stat", "cpu0 0 0")
            command(vm, "cat /proc/mounts", "proc /proc proc ro 0 0")
            first = uptime(vm)
            time.sleep(1.2)
            assert uptime(vm) > first
            before = disk.read_bytes()
            for attempt in ("write /proc/meminfo denied", "touch /proc/net/fake",
                            "mkdir /proc", "rm /proc/net/dev", "mv /proc/stat rename"):
                command(vm, attempt, "\nRead-only filesystem\n")
            assert disk.read_bytes() == before
            command(vm, "cat /proc/net/dev", "\n  eth0:")
            command(vm, "cat /proc/diskstats", "mayfs0")
            vm.shot("procfs-mayshell")
            vm.click(539, 674)
            vm.click(311, 224)
            vm.click(478, 438)
            vm.key("ret")
            vm.shot("procfs-file-manager")
            vm.click(478, 306)
            vm.key("ret")
            vm.shot("procfs-read-only")
            vm.key("ctrl-s")
            assert disk.read_bytes() == before
            vm.key("f1")
            disk_pattern = r"^\s*0\s+0 mayfs0 ((?:\d+ ){10}\d+)$"
            disk_stats = read_fields(vm, "/proc/diskstats", disk_pattern)
            command(vm, "write /persist.txt normal-file", "\nOK\n")
            new_disk_stats = read_fields(vm, "/proc/diskstats", disk_pattern)
            assert int(new_disk_stats[6]) > int(disk_stats[6])
            net_pattern = r"^  eth0: ((?:\d+ ){15}\d+)$"
            net_stats = read_fields(vm, "/proc/net/dev", net_pattern)
            command(vm, "net qemu-probe", "Ethernet ARP reply received")
            new_net_stats = read_fields(vm, "/proc/net/dev", net_pattern)
            assert int(new_net_stats[0]) > int(net_stats[0])
            assert int(new_net_stats[8]) > int(net_stats[8])
        finally:
            vm.stop()
        assert entries(disk)["persist.txt"][3] == "normal-file"
        assert "proc" not in entries(disk)
        print("PASS: live procfs, nested paths, write protection and real MayFS files", flush=True)

        if args.legacy_iso:
            vm = BootVM(iso=args.legacy_iso.resolve(), tag="procfs-legacy-seed", **options)
            try:
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                command(vm, "mkdir /proc", "\nOK\n")
                command(vm, "write /proc/legacy.txt prior-data", "\nOK\n")
            finally:
                vm.stop()
            before = disk.read_bytes()
            vm = BootVM(iso=args.iso.resolve(), tag="procfs-legacy", **options)
            try:
                vm.wait("/proc mount skipped; existing MayFS entry preserved")
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                command(vm, "cat /proc/legacy.txt", "\nprior-data\n")
            finally:
                vm.stop()
            assert disk.read_bytes() == before
            print("PASS: old on-disk /proc directory stays readable and byte-for-byte intact")


if __name__ == "__main__":
    main()

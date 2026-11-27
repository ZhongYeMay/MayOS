#!/usr/bin/env python3
"""Check MayKernel memory accounting and its task manager with real VM sizes."""
import argparse
from pathlib import Path
import time
import re
import shutil
import tempfile

from desktop_menu_smoke import press
from dual_boot_smoke import BUILD, BootVM


def memory_read(vm):
    offset = len(vm.text)
    vm.keyboard_command("cat /proc/meminfo")
    vm.wait("MayUnmanaged:", offset=offset)
    vm.wait(" kB\n", offset=vm.text.index("MayUnmanaged:", offset))
    expected = {"MemTotal", "MemFree", "MemAvailable", "MayDetected", "MayAllocTotal", "MayUnmanaged"}
    values = {}
    for name, value in re.findall(r"^([A-Za-z]+):\s+([0-9]+) kB$", vm.text[offset:], re.MULTILINE):
        values[name] = int(value)
    assert expected <= values.keys(), values
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=BUILD / "mayos.iso")
    parser.add_argument("--ovmf-code", type=Path)
    parser.add_argument("--ovmf-vars", type=Path)
    parser.add_argument("--memory", type=int, nargs="+", default=[256, 512, 1536, 5120])
    args = parser.parse_args()
    if bool(args.ovmf_code) != bool(args.ovmf_vars):
        parser.error("--ovmf-code and --ovmf-vars must be supplied together")
    for mib in args.memory:
        with tempfile.TemporaryDirectory(prefix="mayos-memory-") as root:
            folder = Path(root)
            disk = folder / "empty.img"
            with disk.open("xb") as target:
                target.truncate(8 * 1024 * 1024)
            firmware = {}
            if args.ovmf_code:
                variables = folder / "OVMF_VARS.fd"
                shutil.copyfile(args.ovmf_vars, variables)
                firmware = {"ovmf_code": args.ovmf_code, "ovmf_vars": variables}
            vm = BootVM(iso=args.iso.resolve(), disk=disk, tag=f"task-memory-{mib}",
                        qemu=shutil.which("qemu-system-x86_64"), memory=mib, timeout=55,
                        **firmware)
            try:
                vm.wait("MayKernel64 display modes")
                press(vm, "ret")
                vm.wait("MayOS: desktop ready")
                press(vm, "f1")
                values = memory_read(vm)
                total, detected = values["MemTotal"], values["MayDetected"]
                available = values["MemAvailable"]
                mib_kib = mib * 1024
                # BIOS/UEFI exclude a small firmware area.
                assert mib_kib - 48 * 1024 < detected <= mib_kib, values
                assert 0 < available <= values["MayAllocTotal"] < total <= detected, values
                assert total + values["MayUnmanaged"] == detected, values
                assert values["MemFree"] == available
                if mib >= 5120:
                    assert values["MayUnmanaged"] >= 1024 * 1024
                offset = len(vm.text)
                vm.keyboard_command("memstress")
                vm.wait("memstress: PASS", offset=offset)
                assert memory_read(vm) == values, "Allocator stress leaked pages"
                press(vm, "f2")
                vm.wait("MayOS: Task Manager opened")
                press(vm, "down")
                vm.wait("MayOS Task Manager: showing Memory")
                time.sleep(0.65)
                vm.shot(f"task-memory-{mib}")
            finally:
                vm.stop()
            print(f"PASS: {mib} MiB guest, {detected // 1024} MiB detected, "
                  f"{available // 1024} MiB available, MayKernel Memory page shown", flush=True)


if __name__ == "__main__":
    main()

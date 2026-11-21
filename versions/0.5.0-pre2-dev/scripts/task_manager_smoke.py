#!/usr/bin/env python3
"""Exercise a real QEMU guest Task Manager from F2 on BOTH MayOS kernels."""
from __future__ import annotations
from pathlib import Path
import argparse
import shutil
from dual_boot_smoke import BootVM, BUILD

MARKER = "MayOS: Task Manager opened / CPU GPU DISK RAM NET"

def check(vm, label):
    vm.wait("MayOS: desktop ready", timeout=90)
    vm.key("f2")
    vm.wait(MARKER, timeout=25)
    vm.wait("MayOS: desktop ready")
    # The UI refreshes at 100 Hz PIT-derived elapsed ticks (1 Hz samples).
    # Pause for real frames before saving an actual QMP screenshot.
    import time
    time.sleep(3)
    image = vm.shot(f"task-manager-{label}")
    assert image.is_file() and image.stat().st_size > 10000
    vm.key("f1")
    vm.keyboard_command("perf")
    vm.wait("MayOS: performance snapshot", timeout=20)
    vm.wait("CPU:", timeout=20)
    vm.wait("GPU:", timeout=20)
    vm.wait("DISK:", timeout=20)
    vm.wait("RAM:", timeout=20)
    vm.wait("NET:", timeout=20)
    print(f"PASS: {label} real Task Manager with 5 counters and screenshot: {image}", flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    disk = BUILD / "task-manager-dual-test-disk.img"
    with disk.open("wb") as handle: handle.truncate(8*1024*1024)
    opt = dict(iso=args.iso.resolve(), disk=disk,
               qemu="qemu-system-x86_64", memory=512, timeout=90)
    vm = BootVM(tag="test-task-manager-native", **opt)
    try:
        check(vm, "maykernel")
    finally:
        vm.stop()
    variables = BUILD / "test-task-manager-ovmf-vars.fd"
    shutil.copyfile(args.ovmf_vars, variables)
    vm = BootVM(tag="test-task-manager-linux", **opt,
                ovmf_code=args.ovmf_code, ovmf_vars=variables)
    try:
        vm.wait("Linux ", timeout=30)
        vm.key("down")
        vm.key("ret")
        check(vm, "linux")
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

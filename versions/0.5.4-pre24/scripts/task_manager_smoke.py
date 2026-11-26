#!/usr/bin/env python3
"""Exercise the installed Task Manager windows in a disposable dual-kernel VM."""
import argparse
from pathlib import Path
import shutil
import tempfile
import time

from dual_boot_smoke import BootVM


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--native-iso", type=Path, help="optional standalone MayKernel image for 1080p screenshots")
    parser.add_argument("--linux-kernel", type=Path)
    parser.add_argument("--linux-initramfs", type=Path)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    if bool(args.linux_kernel) != bool(args.linux_initramfs):
        parser.error("--linux-kernel and --linux-initramfs must be supplied together")

    def device_pages(vm, platform):
        vm.key("up")
        vm.wait("MayOS Task Manager: showing GPU")
        for label in ("CPU", "Memory", "Disk", "Network", "GPU"):
            offset = len(vm.text)
            vm.key("down")
            vm.wait(f"MayOS Task Manager: showing {label}", offset=offset, timeout=15)
            vm.shot(f"task-manager-{platform}-{label.lower()}")
        vm.key("down")

    with tempfile.TemporaryDirectory(prefix="mayos-task-test-") as directory:
        root = Path(directory)
        disk = root / "test.img"
        with disk.open("wb") as handle:
            handle.truncate(8 * 1024 * 1024)
        options = dict(iso=args.iso.resolve(), disk=disk, qemu=args.qemu, memory=512, timeout=90)
        native_iso = args.native_iso or (args.iso if args.linux_kernel else None)
        native_options = options | ({"iso": native_iso.resolve()} if native_iso else {})
        vm = BootVM(tag="task-manager-native", **native_options)
        try:
            if native_iso:
                vm.wait("MayKernel64 display modes")
                for key in ("down", "down", "ret"):
                    vm.key(key)
                    time.sleep(.25)
                vm.wait("1920")
                vm.key("down")
                vm.key("ret")
                vm.wait("MayOS: firmware framebuffer 1920x1080")
            else:
                vm.wait("MayOS - custom x86_64 kernel")
                vm.key("ret")
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            vm.keyboard_command("taskmgr")
            vm.wait("MayOS: Task Manager opened")
            time.sleep(2.2)
            device_pages(vm, "maykernel")
            print("PASS: MayKernel opens the native Rust resource window from MayShell", flush=True)
        finally:
            vm.stop()

        if args.linux_kernel:
            options["iso"] = None
            vm = BootVM(tag="task-manager-linux", **options, extra_args=(
                "-kernel", str(args.linux_kernel.resolve()), "-initrd", str(args.linux_initramfs.resolve()),
                "-append", "console=tty0 console=ttyS0,115200 quiet rdinit=/init mayos.desktop=1 vga=0x315",
                "-smp", "4",
            ))
        else:
            variables = root / "ovmf.fd"
            shutil.copyfile(args.ovmf_vars, variables)
            vm = BootVM(tag="task-manager-linux", **options, ovmf_code=args.ovmf_code,
                        ovmf_vars=variables, extra_args=("-smp", "4"))
        try:
            if not args.linux_kernel:
                vm.wait(" - desktop (x86_64)")
                vm.key("down")
                vm.key("ret")
            vm.wait("MayOS: desktop ready")
            vm.wait("MayOS-Linux# ")
            vm.shell_check(
                "mkfifo /tmp/mayos-tm-fifo; "
                "sh -c 'echo mayos-tm-probe > /proc/$$/comm; read line < /tmp/mayos-tm-fifo' & "
                "probe_pid=$!; n=0; "
                "while ! grep -qx mayos-tm-probe /proc/$probe_pid/comm; do "
                "n=$((n+1)); test $n -lt 10 || exit 1; sleep 1; done",
                "PROCESS_CREATED",
            )
            vm.key("f2")
            vm.wait("MayOS: Task Manager opened")
            time.sleep(3)
            device_pages(vm, "linux")
            vm.key("tab")
            vm.shot("task-manager-linux-processes")
            vm.keyboard_command("mayos-tm-probe")
            vm.key("down")
            vm.key("delete")
            vm.shot("task-manager-linux-confirm")
            vm.key("esc")
            vm.shell_check('kill -0 "$probe_pid"', "TERMINATION_CANCELLED")
            vm.key("delete")
            vm.key("ret")
            vm.shell_check(
                'n=0; while kill -0 "$probe_pid" 2>/dev/null; do '
                'n=$((n+1)); test "$n" -lt 10 || exit 1; sleep 1; done',
                "PROCESS_ENDED",
            )
            print("PASS: Linux64 lists a real process, cancels, then confirms its termination", flush=True)
        finally:
            vm.stop()


if __name__ == "__main__":
    main()

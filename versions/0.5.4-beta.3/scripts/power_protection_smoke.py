#!/usr/bin/env python3
"""Check desktop shutdown protection and native document persistence."""

import argparse
import hashlib
from pathlib import Path
import tempfile

from desktop_menu_smoke import application, press, type_text
from dual_boot_smoke import BootVM
from procfs_smoke import command

PROMPT = "MayOS: power action awaiting confirmation"


def open_notes(vm):
    def click_notes():
        vm.click(169, 128)
        vm.click(169, 128)

    application(vm, "editor", click_notes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--dual-iso", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mayos-power-") as work:
        disk = Path(work) / "data.img"
        with disk.open("wb") as image:
            image.truncate(8 * 1024 * 1024)
        options = dict(disk=disk, qemu="qemu-system-x86_64", memory=512, timeout=45, nic="none")
        native = args.iso.resolve()
        vm = BootVM(iso=native, tag="power-persistent", **options)
        try:
            vm.wait("MayKernel64 display modes")
            vm.key("ret")
            vm.wait("MayOS: desktop ready")
            open_notes(vm)
            press(vm, "ctrl-a")
            type_text(vm, "kept-after-restart")
            press(vm, "f1")
            command(vm, "reboot", PROMPT)
            vm.shot("power-unsaved-editor")
            press(vm, "ret")
            assert vm.proc.poll() is None
            open_notes_window = len(vm.text)
            vm.click(480, 122)
            vm.wait("MayOS: active application menu: editor", offset=open_notes_window)
            press(vm, "ctrl-s")
            press(vm, "f1")
            command(vm, "cat Desktop/Notes.txt", "\nkept-after-restart\n")
            vm.keyboard_command("shutdown")
            assert vm.proc.wait(timeout=12) == 0
        finally:
            vm.stop()

        vm = BootVM(iso=native, tag="power-cold-read", **options)
        try:
            vm.wait("MayKernel64 display modes")
            vm.key("ret")
            vm.wait("MayOS: MayFS mounted")
            vm.wait("MayOS: desktop ready")
            press(vm, "f1")
            command(vm, "cat Desktop/Notes.txt", "\nkept-after-restart\n")
        finally:
            vm.stop()
        print("PASS: reboot was cancelled with Enter, then document was saved and survived cold boot", flush=True)

        digest = hashlib.sha256(disk.read_bytes()).digest()
        fault = Path(work) / "fail-writes.conf"
        fault.write_text('[inject-error]\nevent = "write_aio"\nerrno = "5"\n')
        drive = f"file=blkdebug:{fault}:{disk},format=raw,if=ide,index=0,werror=report"
        vm = BootVM(iso=native, disk=None, qemu=options["qemu"], memory=512,
                    timeout=45, nic="none", tag="power-write-failure", extra_args=("-drive", drive))
        try:
            vm.wait("MayKernel64 display modes")
            vm.key("ret")
            vm.wait("MayOS: MayFS mounted")
            vm.wait("MayOS: desktop ready")
            press(vm, "f1")
            command(vm, "write /retry.txt pending-data", "OK in memory (disk write failed)")
            command(vm, "shutdown", PROMPT)
            vm.shot("power-failed-write")
            press(vm, "esc")
            command(vm, "cat /retry.txt", "\npending-data\n")
            assert vm.proc.poll() is None
        finally:
            vm.stop()
        assert hashlib.sha256(disk.read_bytes()).digest() == digest
        print("PASS: disk write failures trigger a warning and keep the session data available", flush=True)

        for mode, iso, marker in (
            ("maykernel", native, "MayKernel64 display modes"),
            ("linux", args.dual_iso.resolve(), "Linux 7.3.1 - desktop"),
        ):
            vm = BootVM(iso=iso, tag=f"power-ram-{mode}", **options)
            try:
                vm.wait(marker)
                vm.key("down")
                vm.key("ret")
                vm.wait("MayOS: desktop ready")
                press(vm, "f1")
                command(vm, "write /scratch.txt session-data", "OK (RAM only; disk sync unavailable)")
                if mode == "maykernel":
                    offset = len(vm.text)
                    vm.click(1074, 15)
                    vm.click(1166, 180)
                    vm.wait(PROMPT, offset=offset)
                else:
                    command(vm, "shutdown", PROMPT)
                vm.shot(f"power-ram-{mode}")
                press(vm, "esc")
                command(vm, "cat /scratch.txt", "\nsession-data\n")
                command(vm, "shutdown", PROMPT)
                vm.move(792, 420)
                # The desktop acts on press, and QMP may close before release.
                vm.q("input-send-event", {"events": [
                    {"type": "btn", "data": {"button": "left", "down": True}}
                ]})
                assert vm.proc.wait(timeout=12) == 0
            finally:
                vm.stop()
            assert hashlib.sha256(disk.read_bytes()).digest() == digest
            print(f"PASS: {mode} protected RAM-only edits until explicit shutdown confirmation", flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Real BIOS/UEFI QEMU checks for one MayKernel+Linux+offline-installer ISO.

Only private, temporary virtual disks are opened or written. The real installer
is selected from the GRUB menu, not bypassed by injecting a QEMU kernel.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ACTIVE = Path(__file__).resolve().parents[1] / "versions/0.5.0-pre2-dev"
sys.path.insert(0, str(ACTIVE / "scripts"))
from dual_boot_smoke import BootVM  # noqa: E402


def checksum(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def blank_disk(root, name, size):
    file = root / name
    with file.open("wb") as stream:
        stream.truncate(size)
    return file


def proof(label, fn):
    try:
        fn()
    except Exception as exc:
        raise AssertionError(f"{label} failed: {exc}") from exc
    print(f"PASS: {label}", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--iso", required=True, type=Path)
    p.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    p.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    p.add_argument("--qemu", default="qemu-system-x86_64")
    args = p.parse_args()
    iso = args.iso.resolve()
    if not iso.is_file() or iso.stat().st_size < 45_000_000:
        p.error("Combined ISO missing/too small")
    screenshots = iso.parent / "screenshots"
    screenshots.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-three-way-qemu-") as folder:
        root = Path(folder)
        disk = blank_disk(root, "unchanged-mayfs.img", 8 * 1024 * 1024)
        options = dict(iso=iso, qemu=args.qemu, memory=512, timeout=105)
        vm = BootVM(disk=disk, tag="triple-maykernel-bios", **options)
        try:
            vm.wait("MayOS: desktop ready", timeout=95)
            vm.wait("MayOS: CPU long mode active (64-bit)")
            shutil.copyfile(vm.shot("triple-maykernel-bios"), screenshots / "MayKernel64-BIOS.png")
            proof("BIOS ISO boots real pre4 MayKernel desktop", lambda: None)
        finally:
            vm.stop()
        initial_sha = checksum(disk)
        vm = BootVM(disk=disk, tag="triple-linux-bios", **options)
        try:
            vm.wait("Start Linux64", timeout=27)
            vm.key("down")
            vm.key("ret")
            vm.wait("MayOS Linux: ready", timeout=105)
            vm.wait("MayOS: desktop ready", timeout=35)
            shutil.copyfile(vm.shot("triple-linux-bios"), screenshots / "Linux64-BIOS.png")
            proof("same BIOS ISO boots real Linux64 desktop", lambda: None)
        finally:
            vm.stop()
        assert checksum(disk) == initial_sha, "Linux accidentally modified MayFS disk"
        print("PASS: Linux boot preserved unrelated MayFS disk byte-for-byte", flush=True)
        # Firmware-independent real installer cancellation on isolated 96MiB disk.
        install_disk = blank_disk(root, "installer-blank-disk.img", 96 * 1024 * 1024)
        ovmf_vars = root / "installer-vars.fd"
        shutil.copyfile(args.ovmf_vars, ovmf_vars)
        opt_uefi = dict(options, ovmf_code=args.ovmf_code, ovmf_vars=ovmf_vars)
        vm = BootVM(disk=install_disk, tag="triple-installer-uefi-cancel", **opt_uefi)
        before = checksum(install_disk)
        try:
            vm.wait("Install/Recover 0.5.2-pre2 (serial console)", timeout=55)
            vm.key("end")
            vm.key("ret")
            vm.wait("Enter the target path", timeout=90)
            shutil.copyfile(vm.shot("triple-installer-uefi-cancel"),
                            screenshots / "Installer-UEFI-console.png")
            vm.send_serial("cancel\n")
            vm.wait("Installation cancelled.", timeout=15)
        finally:
            vm.stop()
        assert checksum(install_disk) == before, "canceled installer changed the disk"
        print("PASS: UEFI x64 boots real serial installer, cancel is byte-for-byte safe", flush=True)
        # The same published installer payload is exercised via the combined ISO,
        # never on a physical host drive. GUI testing remains separate.
        shutil.copyfile(args.ovmf_vars, ovmf_vars)
        vm = BootVM(disk=install_disk, tag="triple-installer-uefi-write", **opt_uefi)
        try:
            vm.wait("Install/Recover 0.5.2-pre2 (serial console)", timeout=55)
            vm.key("end")
            vm.key("ret")
            vm.wait("Enter the target path", timeout=90)
            vm.send_serial("/dev/sda\n")
            vm.wait("To continue, type INSTALL /dev/sda", timeout=30)
            vm.send_serial("INSTALL /dev/sda\n")
            vm.wait("MayOS installation complete.", timeout=95)
            shutil.copyfile(vm.shot("triple-installer-uefi-complete"),
                            screenshots / "Installer-UEFI-installed.png")
        finally:
            vm.stop()
        subprocess.run(["sfdisk", "--verify", str(install_disk)], check=True)
        # Real cold boot of installed disk WITHOUT ISO to verify bootability.
        cold_log = iso.parent / "installed-disk-cold-boot.log"
        qemu = subprocess.Popen([
            args.qemu, "-machine", "pc", "-accel", "tcg", "-cpu", "max",
            "-m", "512", "-vga", "std", "-display", "none", "-nic", "none",
            "-drive", f"file={install_disk},format=raw,if=ide,index=0",
            "-boot", "c", "-serial", f"file:{cold_log}", "-no-reboot",
            "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
            "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 85
            while time.monotonic() < deadline:
                transcript = cold_log.read_text(errors="replace") if cold_log.exists() else ""
                if ("MayOS: MayFS GPT partition selected" in transcript or
                        "MayOS: first boot setup ready" in transcript or
                        "MayOS: desktop ready" in transcript):
                    break
                if qemu.poll() is not None:
                    raise RuntimeError(f"Cold-boot QEMU exited early: {transcript[-1500:]}")
                if "panic" in transcript.lower() or "grub rescue>" in transcript:
                    raise RuntimeError(f"Installed MayKernel failed: {transcript[-1500:]}")
                time.sleep(.25)
            else:
                raise TimeoutError(f"No evidence of ISO-free UEFI MayKernel boot: {cold_log}")
        finally:
            if qemu.poll() is None:
                qemu.terminate()
            qemu.wait(timeout=10)
        text = cold_log.read_text(errors="replace")
        assert "MayOS: " in text, "Installed disk did not boot MayKernel"
        print("PASS: installer wrote a blank 96MiB QEMU disk, GPT verifies, and ISO-free UEFI boot reached MayKernel", flush=True)
        result = {
            "verified_iso_sha256": checksum(iso),
            "bios_maykernel": "PASS", "bios_linux": "PASS",
            "uefi_installer_cancel": "PASS", "uefi_installer_install": "PASS",
            "installed_disk_uefi_cold_boot": "PASS",
            "installer_install_version": "0.5.2-pre2",
        }
        (iso.parent / "qemu-triple-test.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Full isolated QEMU acceptance for official MayOS 0.5.2.

Verifies the REAL live, desktop and installer using one GRUB BIOS/UEFI ISO.
Never accesses physical host disks: all targets are newly-created files in a
temporary directory. Formal release is blocked on *every* assertion below.
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

ROOT = Path(__file__).resolve().parents[1]
STABLE = ROOT / "versions/0.5.2"
sys.path.insert(0, str(STABLE / "scripts"))
from dual_boot_smoke import BootVM  # noqa: E402
from install_mayos import ESP_START, SECTOR  # noqa: E402

VERSION = "0.5.2"
FINAL = "MayOS-0.5.2-DualKernel-Live-Installer-x86_64.iso"
SERIAL_INSTALLER = "Install/Recover MayOS 0.5.2 (serial console)"
LIVE_MENU = "MayOS Live 0.5.2 - RAM only / no disk writes"
LIVE_MARKER = "MayOS Live 0.5.2: RAM-only session; disk writes blocked"
LIVE_FS_MARKER = "MayOS Live: session filesystem runs in RAM; disk writes disabled"
REQUIRED = {
    "bios_maykernel": "PASS",
    "bios_linux": "PASS",
    "bios_live_no_disk_writes": "PASS",
    "uefi_live_no_disk_writes": "PASS",
    "uefi_installer_cancel_without_writes": "PASS",
    "uefi_installer_install": "PASS",
    "uefi_installed_slot_exact_sha256": "PASS",
    "uefi_iso_free_cold_boot": "PASS",
}


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def make_disk(root: Path, name: str, size: int):
    path = root / name
    with path.open("xb") as stream:
        stream.truncate(size)
    return path


def choose(vm, entry: str, downs: int):
    vm.wait(entry, timeout=42)
    for _ in range(downs):
        vm.key("down")
        time.sleep(.06)
    vm.key("ret")


def take(vm, screenshots: Path, name: str):
    source = vm.shot(name)
    target = screenshots / (name + ".png")
    shutil.copyfile(source, target)
    # A uniform serial/GRUB framebuffer compresses to far below 10 KiB.
    # Verify real PNG structure and full framebuffer dimensions instead.
    header = target.read_bytes()[:24]
    assert header[:8] == b"\x89PNG\r\n\x1a\n", name
    assert len(header) >= 24, name
    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    assert width >= 640 and height >= 480, (name, width, height)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    args = parser.parse_args()
    # Cached ISO acceptance runs need the QEMU serial/QMP log directory even
    # when the kernel compiler itself was intentionally skipped.
    (STABLE / "build").mkdir(parents=True, exist_ok=True)
    iso = args.iso.resolve(strict=True)
    if iso.name != FINAL:
        parser.error("Wrong official release ISO filename")
    manifest = json.loads((iso.parent / (iso.stem + ".manifest.json")).read_text())
    if manifest["release"] != VERSION or manifest["sha256"] != sha(iso):
        parser.error("Manifest does not authenticate the requested ISO")
    result = dict(REQUIRED)
    for key in result:
        result[key] = "NOT_TESTED"
    screens = iso.parent / "screenshots"
    screens.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-v052-safe-qemu-") as tmp:
        root = Path(tmp)
        mayfs = make_disk(root, "live-protected-disk.img", 8 * 1024 * 1024)
        opts = dict(iso=iso, qemu=args.qemu, memory=512, timeout=110)

        vm = BootVM(disk=mayfs, tag="official-bios-maykernel", **opts)
        try:
            vm.wait("MayOS: CPU long mode active (64-bit)", timeout=95)
            vm.wait("MayOS Workstation 0.5.2 / MayShell")
            vm.wait("MayOS: desktop ready", timeout=55)
            take(vm, screens, "BIOS-MayKernel64-0.5.2")
        finally:
            vm.stop()
        result["bios_maykernel"] = "PASS"
        print("PASS: same ISO boots 0.5.2 MayKernel64 BIOS desktop", flush=True)

        vm = BootVM(disk=mayfs, tag="official-bios-linux64", **opts)
        try:
            choose(vm, "Linux 7.3.1 - desktop", 1)
            vm.wait("MayOS Linux: ready", timeout=110)
            vm.wait("MayOS: desktop ready", timeout=60)
            take(vm, screens, "BIOS-Linux64-7.3.1")
        finally:
            vm.stop()
        result["bios_linux"] = "PASS"
        print("PASS: same ISO boots Linux64 7.3.1 desktop", flush=True)

        # The same real MayFS disk is byte-for-byte compared before and after
        # interaction inside RAM-only Live, not just an idle boot.
        before = sha(mayfs)
        vm = BootVM(disk=mayfs, tag="official-bios-live", **opts)
        try:
            choose(vm, LIVE_MENU, 4)
            vm.wait(LIVE_MARKER, timeout=85)
            vm.wait(LIVE_FS_MARKER, timeout=85)
            vm.wait("MayOS: desktop ready", timeout=50)
            vm.key("f1")
            vm.keyboard_command("write live-ephemeral.txt session-not-persistent")
            vm.wait("live-ephemeral.txt", timeout=30)
            take(vm, screens, "BIOS-MayOS-Live-0.5.2")
        finally:
            vm.stop()
        assert sha(mayfs) == before, "MayOS Live wrote to an attached disk!"
        result["bios_live_no_disk_writes"] = "PASS"
        print("PASS: BIOS MayOS Live created RAM file, physical MayFS disk SHA unchanged", flush=True)

        vars_path = root / "live-ovmf-vars.fd"
        shutil.copyfile(args.ovmf_vars, vars_path)
        efi_opts = dict(opts, ovmf_code=args.ovmf_code, ovmf_vars=vars_path)
        before = sha(mayfs)
        vm = BootVM(disk=mayfs, tag="official-uefi-live", **efi_opts)
        try:
            choose(vm, LIVE_MENU, 4)
            vm.wait(LIVE_MARKER, timeout=100)
            vm.wait(LIVE_FS_MARKER, timeout=55)
            vm.wait("MayOS: desktop ready", timeout=45)
            take(vm, screens, "UEFI-MayOS-Live-0.5.2")
        finally:
            vm.stop()
        assert sha(mayfs) == before, "UEFI Live mode modified disk"
        result["uefi_live_no_disk_writes"] = "PASS"
        print("PASS: UEFI x64 Live boots without writing physical disk", flush=True)

        disk = make_disk(root, "installation-blank-target.img", 96 * 1024 * 1024)
        shutil.copyfile(args.ovmf_vars, vars_path)
        before = sha(disk)
        vm = BootVM(disk=disk, tag="official-uefi-cancel", **efi_opts)
        try:
            vm.wait(SERIAL_INSTALLER, timeout=55)
            vm.key("end")
            vm.key("ret")
            vm.wait("MayOS 0.5.2 | Installation and recovery", timeout=85)
            vm.wait("Enter the target path", timeout=55)
            take(vm, screens, "UEFI-Installer-0.5.2")
            vm.send_serial("cancel\n")
            vm.wait("Installation cancelled.", timeout=25)
        finally:
            vm.stop()
        assert sha(disk) == before, "Canceled install changed disk"
        result["uefi_installer_cancel_without_writes"] = "PASS"
        print("PASS: UEFI installer cancellation preserved blank disk SHA", flush=True)

        shutil.copyfile(args.ovmf_vars, vars_path)
        vm = BootVM(disk=disk, tag="official-uefi-install", **efi_opts)
        try:
            vm.wait(SERIAL_INSTALLER, timeout=55)
            vm.key("end")
            vm.key("ret")
            vm.wait("MayOS 0.5.2 | Installation and recovery", timeout=85)
            vm.wait("Enter the target path", timeout=55)
            vm.send_serial("/dev/sda\n")
            vm.wait("To continue, type INSTALL /dev/sda", timeout=30)
            vm.send_serial("INSTALL /dev/sda\n")
            vm.wait("MayOS installation complete.", timeout=95)
            take(vm, screens, "UEFI-Installer-Complete-0.5.2")
        finally:
            vm.stop()
        subprocess.run(["sfdisk", "--verify", str(disk)], check=True)
        result["uefi_installer_install"] = "PASS"
        print("PASS: real UEFI installer wrote GPT to disposable empty QEMU disk", flush=True)

        installed_kernel = iso.parent / "installed-A.elf"
        subprocess.run(["mcopy", "-i", f"{disk}@@{ESP_START * SECTOR}",
                        "::/slots/A.elf", str(installed_kernel)], check=True)
        expected_kernel = manifest["boot_modes"][0]["kernel_sha256"]
        if sha(installed_kernel) != expected_kernel:
            raise AssertionError("Installed ELF not byte-identical to live 0.5.2 kernel")
        if manifest["boot_modes"][1]["kernel_sha256"] != expected_kernel:
            raise AssertionError("Live mode not using same 0.5.2 ELF")
        if b"MayOS Workstation 0.5.2 / MayShell" not in installed_kernel.read_bytes():
            raise AssertionError("Installer wrote kernel with incorrect version")
        result["uefi_installed_slot_exact_sha256"] = "PASS"
        print("PASS: installed A slot SHA-256 equals live AND normal desktop ELF", flush=True)

        # Remove ISO entirely for a genuine installed GPT UEFI cold boot.
        log = iso.parent / "installed-v052-cold-boot.log"
        qemu = subprocess.Popen([
            args.qemu, "-machine", "pc", "-accel", "tcg", "-cpu", "max",
            "-m", "512", "-vga", "std", "-display", "none", "-nic", "none",
            "-drive", f"file={disk},format=raw,if=ide,index=0",
            "-boot", "c", "-serial", f"file:{log}", "-no-reboot",
            "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
            "-drive", f"if=pflash,format=raw,file={vars_path}",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 100
            while time.monotonic() < deadline:
                transcript = log.read_text(errors="replace") if log.is_file() else ""
                if ("MayOS: MayFS GPT partition selected" in transcript and
                        "MayOS: first boot setup ready" in transcript):
                    break
                if qemu.poll() is not None:
                    raise RuntimeError(f"ISO-free UEFI guest quit early: {transcript[-1500:]}")
                if "kernel panic" in transcript.lower() or "grub rescue>" in transcript:
                    raise RuntimeError(f"Installed kernel failed: {transcript[-1500:]}")
                time.sleep(.2)
            else:
                raise TimeoutError("Installed system did not reach real first-boot setup")
        finally:
            if qemu.poll() is None:
                qemu.terminate()
            qemu.wait(timeout=10)
        result["uefi_iso_free_cold_boot"] = "PASS"
        print("PASS: ISO removed, UEFI installed 0.5.2 reaches GPT+first boot setup", flush=True)
    assert result == REQUIRED, result
    result["iso_sha256"] = sha(iso)
    result["release"] = VERSION
    (iso.parent / "qemu-v052-official-acceptance.json").write_text(
        json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)


if __name__ == "__main__":
    main()

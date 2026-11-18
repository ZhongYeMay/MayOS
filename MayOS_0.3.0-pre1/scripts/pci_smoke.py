#!/usr/bin/env python3
"""Boot MayKernel with a mixed standards-based PCI topology."""
import os
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
QEMU = os.environ.get("QEMU", "qemu-system-x86_64")


def main():
    log = BUILD / "test-pci-topology.log"
    command = [
        QEMU, "-machine", "pc", "-accel", "tcg", "-cpu", "max", "-m", "256",
        "-vga", "std", "-display", "none", "-cdrom", str(BUILD / "mayos.iso"),
        "-boot", "d", "-serial", f"file:{log}", "-no-reboot", "-no-shutdown",
        "-nic", "none",
        "-device", "ich9-ahci,id=generic-ahci",
        "-device", "qemu-xhci,id=generic-xhci",
        "-device", "virtio-net-pci,id=generic-network",
        "-device", "intel-hda,id=generic-audio",
    ]
    if os.environ.get("QEMU_DATA_DIR"):
        command += ["-L", os.environ["QEMU_DATA_DIR"]]
    log.unlink(missing_ok=True)
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 35
        required = {
            "class 01:06": "AHCI storage controller",
            "class 0c:03:30": "xHCI USB controller",
            "class 02:": "network controller",
            "class 04:03": "HDA audio controller",
            "MayOS: desktop ready": "desktop boot after enumeration",
        }
        while time.monotonic() < deadline:
            text = log.read_text(errors="replace") if log.exists() else ""
            if all(token in text for token in required):
                break
            if process.poll() is not None:
                error = process.stderr.read() if process.stderr else ""
                raise AssertionError(f"QEMU exited during PCI probe: {error}\n{text}")
            time.sleep(.1)
        else:
            missing = [description for token, description in required.items()
                       if token not in text]
            raise AssertionError(f"PCI topology probe timed out; missing {missing}:\n{text}")
        print("PASS: generic PCI enumeration classified IDE/AHCI/xHCI/network/audio and booted")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()

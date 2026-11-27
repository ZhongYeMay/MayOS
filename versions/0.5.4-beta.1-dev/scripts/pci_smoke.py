#!/usr/bin/env python3
"""Boot MayKernel with a mixed standards-based PCI topology."""
import os
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
QEMU = os.environ.get("QEMU", "qemu-system-x86_64")


def run_case(name, machine, devices, required):
    log = BUILD / f"test-pci-{name}.log"
    command = [
        QEMU, "-machine", machine, "-accel", "tcg", "-cpu", "max", "-m", "256",
        "-vga", "std", "-display", "none", "-cdrom", str(BUILD / "mayos.iso"),
        "-boot", "d", "-serial", f"file:{log}", "-no-reboot", "-no-shutdown",
        "-nic", "none",
    ]
    for device in devices:
        command += ["-device", device]
    if os.environ.get("QEMU_DATA_DIR"):
        command += ["-L", os.environ["QEMU_DATA_DIR"]]
    log.unlink(missing_ok=True)
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic() + 35
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
        print(f"PASS: {name} PCI topology")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def main():
    run_case("legacy-mixed", "pc", [
        "ich9-ahci,id=generic-ahci",
        "qemu-xhci,id=generic-xhci",
        "usb-kbd,bus=generic-xhci.0",
        "virtio-net-pci,id=generic-network",
        "e1000,id=generic-e1000",
        "intel-hda,id=generic-audio",
        "hda-duplex,bus=generic-audio.0",
    ], {
        "class 01:06": "AHCI storage controller",
        "class 0c:03:30": "xHCI USB controller",
        "xHCI generic controller ready, controllers=1 connected-ports=1":
            "initialized xHCI rings and detected a standards-based USB port",
        "class 02:": "network controller",
        "E1000 generic Ethernet rings ready": "E1000 DMA receive/transmit rings",
        "class 04:03": "HDA audio controller",
        "HDA generic CORB/RIRB ready, codecs=1": "HDA codec command transport",
        "MayOS: desktop ready": "desktop boot after enumeration",
    })
    run_case("pcie-ecam", "q35", [], {
        "MCFG ranges=1": "ACPI PCIe MCFG range",
        "class 01:06": "q35 AHCI controller through ECAM",
        "MayOS: desktop ready": "desktop boot through PCIe enumeration",
    })
    print("PASS: generic PCI/PCIe discovery covers legacy config and ACPI ECAM")


if __name__ == "__main__":
    main()

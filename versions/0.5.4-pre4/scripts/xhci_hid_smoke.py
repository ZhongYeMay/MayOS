#!/usr/bin/env python3
"""Exercise USB-only input, high MMIO NVMe and the read-only trial mode."""
import argparse
from contextlib import contextmanager
from pathlib import Path
import shutil
import tempfile

from dual_boot_smoke import BootVM
from live_upgrade import digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mayos-xhci-") as directory:
        root = Path(directory)
        sata = root / "unused-sata.img"
        nvme = root / "mayfs-nvme.img"
        for image in (sata, nvme):
            with image.open("wb") as file:
                file.truncate(8 * 1024 * 1024)
        original_sata = digest(sata)

        @contextmanager
        def boot(tag, trial=False):
            variables = root / f"{tag}.fd"
            shutil.copyfile(args.ovmf_vars, variables)
            vm = BootVM(iso=args.iso.resolve(), disk=sata, tag=tag, qemu="qemu-system-x86_64",
                        memory=512, timeout=40, ovmf_code=args.ovmf_code, ovmf_vars=variables,
                        machine="q35,i8042=off", extra_args=(
                            "-device", "qemu-xhci,id=usb0",
                            "-device", "qemu-xhci,id=usb1",
                            "-device", "usb-kbd,bus=usb0.0,id=keyboard",
                            "-device", "usb-mouse,bus=usb1.0,id=mouse",
                            "-drive", f"file={nvme},format=raw,if=none,id=nvme_drive",
                            "-device", "nvme,drive=nvme_drive,serial=MAYOSTEST"))
            try:
                if trial:
                    vm.wait("MayOS Live 0.5.4-pre4")
                    vm.send_serial("\x1b[B\r")
                    vm.wait("MayOS Live 0.5.4-pre4: RAM-only session; disk writes blocked")
                vm.wait("legacy PS/2 controller absent")
                vm.wait("xHCI uses a 64-bit high MMIO BAR")
                vm.wait("controllers=2 connected-ports=2")
                vm.wait("NVMe uses a 64-bit high MMIO BAR")
                vm.wait("NVMe generic namespace ready")
                vm.wait("MayOS: desktop ready")
                vm.wait("USB boot keyboard ready")
                vm.wait("USB boot mouse ready")
                yield vm
            finally:
                vm.stop()

        def command(vm, value, expected):
            offset = len(vm.text)
            vm.keyboard_command(value)
            return vm.wait(expected, offset=offset)[offset:]

        with boot("usb-hid-uefi") as vm:
            vm.key("meta_l")
            vm.wait("MayOS: application launcher opened")
            vm.key("f1")
            lines = command(vm, "hardware", "Storage: writable MayFS")
            assert "USB boot keyboards: 1" in lines and "USB boot mice: 1" in lines
            vm.shot("usb-hid-hardware")
            vm.click(580, 674)  # Files, opened using the second controller's mouse.
            vm.click(479, 212)  # Create file.
            vm.keyboard_command("usb-mouse.txt")
            vm.shot("usb-hid-mouse")
            vm.key("f1")
            command(vm, "ls Documents", "usb-mouse.txt")
            for index in range(3):  # Larger than one 255-entry keyboard transfer ring.
                payload = "usb-input-" + "x" * 30 + str(index)
                command(vm, "write ring" + str(index) + ".txt " + payload, "\nOK\n")
            vm.q("device_del", {"id": "keyboard"})
            vm.wait("USB boot keyboard disconnected")
            offset = len(vm.text)
            vm.q("device_add", {"driver": "usb-kbd", "bus": "usb0.0", "id": "keyboard2"})
            vm.wait("USB boot keyboard ready", offset=offset)
            command(vm, "cat ring2.txt", "\n" + payload + "\n")
            vm.shot("usb-hid-nvme")
        with boot("usb-hid-cold") as vm:
            vm.key("f1")
            command(vm, "cat ring2.txt", "\n" + payload + "\n")
        assert digest(sata) == original_sata
        print("PASS: UEFI USB-only input on two xHCI controllers, reconnect, and persistent high-MMIO NVMe", flush=True)

        before = digest(nvme)
        with boot("usb-hid-ram-trial", trial=True) as vm:
            vm.key("f1")
            command(vm, "hardware", "Storage: RAM-only hardware trial")
            command(vm, "write local.txt ram-only", "OK (RAM only; disk sync unavailable)")
            command(vm, "cat local.txt", "\nram-only\n")
            vm.shot("usb-hid-ram")
        assert digest(nvme) == before and digest(sata) == original_sata
        print("PASS: RAM-only trial keeps the SATA and NVMe images byte-for-byte unchanged", flush=True)


if __name__ == "__main__":
    main()

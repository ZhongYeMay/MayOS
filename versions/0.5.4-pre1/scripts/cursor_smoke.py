#!/usr/bin/env python3
"""Verify CUR and ANI import from the real MayKernel framebuffer."""
import argparse
from contextlib import contextmanager
from pathlib import Path
import shutil
import tempfile

from PIL import Image

from dual_boot_smoke import BootVM, BUILD
from live_upgrade import digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cur-iso", type=Path, required=True)
    parser.add_argument("--ani-iso", type=Path, required=True)
    parser.add_argument("--linux-iso", type=Path, help="Dual-kernel ISO with the ANI imported")
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mayos-cursor-") as tmp:
        root = Path(tmp)
        disk = root / "nvram.img"

        @contextmanager
        def boot(iso, tag, uefi, linux=False):
            variables = root / f"{tag}.fd"
            firmware = {}
            if uefi:
                shutil.copyfile(args.ovmf_vars, variables)
                firmware = dict(ovmf_code=args.ovmf_code, ovmf_vars=variables)
            vm = BootVM(iso=iso.resolve(), disk=disk, tag=tag, qemu="qemu-system-x86_64",
                        memory=256, timeout=40, **firmware)
            try:
                if linux:
                    vm.wait(" - desktop (x86_64)")
                    vm.key("down")
                    vm.key("ret")
                else:
                    vm.wait("MayOS: pointer asset loaded from boot media")
                vm.wait("MayOS: desktop ready")
                yield vm
            finally:
                vm.stop()

        def start_disk():
            with disk.open("wb") as file:
                file.truncate(8 * 1024 * 1024)

        def apply_pointer(vm):
            vm.click(637, 674)
            vm.click(330, 398)
            vm.shot("windows-pointer-settings")
            vm.click(952, 519)
            vm.wait("MayOS: custom pointer applied: ISO")
            vm.move(1065, 393)

        def capture(vm, label):
            vm.shot(label)
            with Image.open(BUILD / f"{label}.png") as screen:
                rgb = screen.convert("RGB").crop((1045, 373, 1090, 470)).tobytes()
                return set(zip(rgb[::3], rgb[1::3], rgb[2::3]))

        start_disk()
        with boot(args.cur_iso, "pointer-cur-bios", False) as vm:
            apply_pointer(vm)
            assert (64, 230, 190) in capture(vm, "windows-pointer-cur")
        print("PASS: PNG-backed CUR imported and applied on BIOS", flush=True)

        start_disk()
        with boot(args.ani_iso, "pointer-ani-uefi", True) as vm:
            apply_pointer(vm)
            observed = set()
            for index in range(7):
                observed |= capture(vm, f"windows-pointer-animation-{index}")
            assert {(64, 230, 190), (240, 110, 85)} <= observed
        with boot(args.ani_iso, "pointer-ani-restored", True) as vm:
            vm.wait("MayOS: custom pointer restored: ISO")
            vm.click(637, 674)
            vm.click(330, 398)
            vm.click(909, 269)  # Restore the stock pointer and size.
            vm.move(1065, 393)
            default = capture(vm, "windows-pointer-default-restored")
            assert (64, 230, 190) not in default and (240, 110, 85) not in default
        with boot(args.ani_iso, "pointer-default-cold", True) as vm:
            assert "custom pointer restored" not in vm.text
        print("PASS: UEFI ANI frames advance without mouse movement, survive restart, and reset", flush=True)
        if args.linux_iso:
            before = digest(disk)
            with boot(args.linux_iso, "pointer-linux-uefi", True, linux=True) as vm:
                apply_pointer(vm)
                observed = set()
                for index in range(7):
                    observed |= capture(vm, f"windows-pointer-linux-{index}")
                assert {(64, 230, 190), (240, 110, 85)} <= observed
            assert digest(disk) == before
            print("PASS: Linux64 imports and animates the ANI without changing MayFS", flush=True)


if __name__ == "__main__":
    main()

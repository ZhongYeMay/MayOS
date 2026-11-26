#!/usr/bin/env python3
"""Type through the real MayOS editor and verify exact CJK bytes."""
import argparse
from contextlib import contextmanager
from pathlib import Path
import shutil
import tempfile

from dual_boot_smoke import BootVM
from live_upgrade import digest

EXPECTED = "\n你好\n你好\n日本語\nコーヒー\n한글\n👩\n"


def keys(vm, value):
    for key in value:
        vm.key({"-": "minus", "/": "slash", "\n": "ret"}.get(key, key))


def type_document(vm, prefix):
    vm.click(580, 674)
    vm.click(479, 212)
    vm.keyboard_command("ime-proof.txt")
    vm.key("ret")
    vm.key("f3")
    keys(vm, "nihao")
    vm.shot(f"{prefix}-pinyin")
    vm.key("1")
    vm.key("ret")
    vm.key("f3")
    keys(vm, "su\ncl\n\n")
    vm.key("f3")
    keys(vm, "nihongo")
    vm.key("spc")
    vm.shot(f"{prefix}-japanese")
    keys(vm, "\n\n")
    vm.key("f3")
    keys(vm, "ko-hi-\n\n")
    vm.key("f3")
    keys(vm, "gksrmfx")
    vm.key("backspace")
    vm.shot(f"{prefix}-hangul")
    keys(vm, "\n\n")
    vm.key("f3")
    keys(vm, "d800\n")
    vm.key("esc")  # A surrogate cannot be inserted.
    keys(vm, "1f469\n200d\n1f4bb\n")
    vm.key("backspace")  # Remove the complete woman-technologist cluster.
    keys(vm, "1f469\n")
    vm.key("ctrl-s")
    vm.click(580, 674)
    vm.click(479, 212)
    vm.key("f3")
    keys(vm, "wenjian\n")
    vm.shot(f"{prefix}-filename")
    vm.key("ret")
    read_document(vm)


def read_document(vm):
    vm.key("f1")
    offset = len(vm.text)
    vm.keyboard_command("cat Documents/ime-proof.txt")
    vm.wait(EXPECTED, offset=offset)
    offset = len(vm.text)
    vm.keyboard_command("ls Documents")
    vm.wait("文件", offset=offset)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--dual-iso", type=Path)
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mayos-ime-test-") as directory:
        root = Path(directory)
        disk = root / "mayos.img"
        with disk.open("wb") as handle:
            handle.truncate(8 * 1024 * 1024)

        @contextmanager
        def boot(iso, tag, linux=False):
            firmware = {}
            if linux:
                variables = root / f"{tag}.fd"
                shutil.copyfile(args.ovmf_vars, variables)
                firmware.update(ovmf_code=args.ovmf_code, ovmf_vars=variables)
            vm = BootVM(iso=iso.resolve(), disk=disk, tag=tag, qemu="qemu-system-x86_64",
                        memory=512, timeout=45, **firmware)
            try:
                if linux:
                    vm.wait(" - desktop (x86_64)")
                    vm.key("down")
                    vm.key("ret")
                vm.wait("MayOS: desktop ready")
                yield vm
            finally:
                vm.stop()

        with boot(args.iso, "ime-native") as vm:
            type_document(vm, "ime-native")
        with boot(args.iso, "ime-native-cold") as vm:
            read_document(vm)
        print("PASS: MayKernel64 CJK input, valid 4-byte Unicode and cold-boot exact text", flush=True)
        if args.dual_iso:
            before = digest(disk)
            with boot(args.dual_iso, "ime-linux", linux=True) as vm:
                type_document(vm, "ime-linux")
            assert digest(disk) == before
            print("PASS: Linux64 uses the same editor and leaves MayFS byte-for-byte intact", flush=True)


if __name__ == "__main__":
    main()

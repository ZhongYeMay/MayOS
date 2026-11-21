#!/usr/bin/env python3
"""Boot a clean installed disk without optical media and verify data migration."""
from pathlib import Path
import shutil
import tempfile

from dual_boot_smoke import BootVM, BUILD
from install_mayos import DATA_START, SECTOR, install
from live_upgrade import digest


def main():
    iso = BUILD / "mayos.iso"
    code = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    template = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        legacy = root / "legacy.img"
        with legacy.open("xb") as out:
            out.truncate(8 * 1024 * 1024)

        def boot(disk, tag, *, media=None, installed=False, commands=()):
            vars_path = root / f"{tag}.fd"
            shutil.copyfile(template, vars_path)
            vm = BootVM(iso=media, disk=disk, tag=tag, qemu="qemu-system-x86_64",
                        memory=256, timeout=60, ovmf_code=code, ovmf_vars=vars_path)
            try:
                vm.wait("MayOS: desktop ready")
                if installed:
                    vm.wait("MayOS: MayFS GPT partition selected")
                vm.key("f1")
                for cmd, result in commands:
                    offset = len(vm.text)
                    vm.keyboard_command(cmd)
                    vm.wait(result, offset=offset)
            finally:
                vm.stop()

        boot(legacy, "installer-legacy-seed", media=iso,
             commands=[("write keep.txt original", "\nOK\n")])
        before = digest(legacy)
        installed = root / "installed.img"
        install(iso, digest(iso), installed, legacy)
        assert digest(legacy) == before
        with installed.open("rb") as handle:
            prefix = handle.read(DATA_START * SECTOR)
        boot(installed, "installer-installed", installed=True, commands=[
            ("cat keep.txt", "\noriginal\n"),
            ("write fresh.txt installed", "\nOK\n"),
        ])
        boot(installed, "installer-cold-boot", installed=True, commands=[
            ("cat keep.txt", "\noriginal\n"), ("cat fresh.txt", "\ninstalled\n"),
        ])
        with installed.open("rb") as handle:
            assert handle.read(DATA_START * SECTOR) == prefix
        print("PASS: UEFI without ISO, legacy MayFS import and cold-boot writes within the GPT data partition")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Boot a clean installed disk without optical media and verify data migration."""
from pathlib import Path
import shutil
import tempfile

from dual_boot_smoke import BootVM, BUILD
from install_mayos import DATA_START, DATA_BLOCKS, SECTOR, install, rollback, upgrade
from live_upgrade import digest


def main():
    iso = BUILD / "installer-base.iso"
    candidate = BUILD / "mayos.iso"
    code = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    template = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        legacy = root / "legacy.img"
        with legacy.open("xb") as out:
            out.truncate(8 * 1024 * 1024)

        def boot(disk, tag, *, media=None, installed=False, commands=(), version=None, setup=False):
            vars_path = root / f"{tag}.fd"
            shutil.copyfile(template, vars_path)
            vm = BootVM(iso=media, disk=disk, tag=tag, qemu="qemu-system-x86_64",
                        memory=256, timeout=60, ovmf_code=code, ovmf_vars=vars_path)
            try:
                if setup:
                    for prompt in ("setup", "appearance", "review"):
                        vm.wait(f"MayOS: first boot {prompt} ready")
                        vm.key("ret")
                    vm.wait("MayOS: first boot setup completed")
                vm.wait("MayOS: desktop ready")
                if installed:
                    vm.wait("MayOS: MayFS GPT partition selected")
                if version:
                    vm.wait(f"MayOS Workstation {version} / MayShell")
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
        unconfigured = digest(installed)
        upgrade(installed, candidate, digest(candidate), root / "unconfigured-upgrade.img",
                "0.5.1-pre2", code, template)
        assert digest(installed) == unconfigured
        print("PASS: trial accepts a system awaiting first boot without changing its source disk")
        with installed.open("rb") as handle:
            prefix = handle.read(DATA_START * SECTOR)
        boot(installed, "installer-installed", installed=True, setup=True, commands=[
            ("cat keep.txt", "\noriginal\n"),
            ("write fresh.txt installed", "\nOK\n"),
        ])
        boot(installed, "installer-cold-boot", installed=True, commands=[
            ("cat keep.txt", "\noriginal\n"), ("cat fresh.txt", "\ninstalled\n"),
        ])
        with installed.open("rb") as handle:
            assert handle.read(DATA_START * SECTOR) == prefix
        print("PASS: UEFI without ISO, legacy MayFS import and cold-boot writes within the GPT data partition")
        before = digest(installed)
        rejected = root / "rejected.img"
        try:
            upgrade(installed, candidate, digest(candidate), rejected, "0.5.1-pre99", code, template)
        except ValueError as error:
            assert "Installed-disk trial failed" in str(error), error
        else:
            raise AssertionError("Wrong trial version published an image")
        assert not rejected.exists() and digest(installed) == before
        print("PASS: booted candidate with wrong version, original installed disk preserved")

        upgraded = root / "upgraded.img"
        upgrade(installed, candidate, digest(candidate), upgraded, "0.5.1-pre2", code, template)
        assert digest(installed) == before
        with installed.open("rb") as old, upgraded.open("rb") as new:
            for stream in (old, new):
                stream.seek(DATA_START * SECTOR)
            assert old.read(DATA_BLOCKS * SECTOR) == new.read(DATA_BLOCKS * SECTOR)
        boot(upgraded, "installer-upgraded", installed=True, version="0.5.1-pre2", commands=[
            ("cat keep.txt", "\noriginal\n"), ("cat fresh.txt", "\ninstalled\n"),
            ("write after.txt after-upgrade", "\nOK\n"),
        ])
        print("PASS: verified ISO upgrade with byte-identical MayFS and no optical media")
        rolled_back = root / "rolled-back.img"
        rollback(upgraded, rolled_back, code, template)
        boot(rolled_back, "installer-rollback", installed=True, version="0.5.1-pre1", commands=[
            ("cat keep.txt", "\noriginal\n"), ("cat after.txt", "\nafter-upgrade\n"),
        ])
        print("PASS: booted previous installed slot and read data written after the upgrade")


if __name__ == "__main__":
    main()

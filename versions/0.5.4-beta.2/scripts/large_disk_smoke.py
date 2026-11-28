#!/usr/bin/env python3
"""Check 64-bit GPT on sparse, disposable QEMU disks."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from dual_boot_smoke import BUILD, BootVM
from install_mayos import clone_installed, install
from installed_storage_smoke import complete_setup
from live_upgrade import digest

GIB = 1024 ** 3
TIB = 1024 ** 4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mayos", type=Path, default=BUILD / "mayos.iso")
    parser.add_argument("--installer", type=Path, default=BUILD / "mayos-installer.iso")
    parser.add_argument("--ovmf-code", type=Path, required=True)
    parser.add_argument("--ovmf-vars", type=Path, required=True)
    parser.add_argument("--full-install", action="store_true", help="Also scan and install on a blank 129 GiB disk")
    args = parser.parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        parser.error("qemu-system-x86_64 is required")

    with tempfile.TemporaryDirectory(prefix="mayos-large-disk-") as folder:
        work = Path(folder)

        def start(tag, disk, mode, optical=None, timeout=65):
            variables = work / f"{tag}.fd"
            shutil.copyfile(args.ovmf_vars, variables)
            wrapper = work / f"qemu-{tag}"
            drive = f"file={disk},format=raw,if=none,id=target,discard=unmap,detect-zeroes=unmap"
            hardware = ("nvme,drive=target,serial=MAYOS-LARGE,bootindex=1" if mode == "nvme"
                        else "ide-hd,bus=ide.0,drive=target,bootindex=1")
            wrapper.write_text(f"""#!/usr/bin/env python3
import os
import sys
args = sys.argv[1:]
i = args.index('-drive')
args[i:i+2] = ['-drive', {drive!r}, '-device', {hardware!r}]
if '-cdrom' in args:
    i = args.index('-cdrom')
    media = args[i+1]
    args[i:i+2] = ['-drive','file='+media+',if=none,media=cdrom,id=installer',
                   '-device','ide-cd,bus=ide.1,drive=installer,bootindex=0']
os.execv({qemu!r}, [{qemu!r}, *args])
""")
            wrapper.chmod(0o755)
            return BootVM(iso=optical, disk=disk, tag=tag, qemu=str(wrapper), memory=512, timeout=timeout,
                          machine="q35", ovmf_code=args.ovmf_code, ovmf_vars=variables)

        def install_command(vm):
            vm.wait("Install MayOS (serial console)")
            vm.send_serial("\x1b[B\r")
            vm.wait("Enter the target path")
            vm.send_serial("/dev/nvme0n1\n")
            vm.wait("To continue, type INSTALL /dev/nvme0n1")
            vm.send_serial("INSTALL /dev/nvme0n1\n")

        protected = work / "protected.img"
        marker = b"Existing data must be preserved"
        locations = (4096, 512 * GIB - 4096)
        with protected.open("xb") as f:
            f.truncate(512 * GIB)
            for offset in locations:
                f.seek(offset)
                f.write(marker)
        before = protected.stat().st_blocks
        vm = start("large-nonblank", protected, "nvme", args.installer.resolve())
        try:
            install_command(vm)
            vm.wait("Target contains data. No sectors were written.")
            assert "Writing and verifying the installed MayOS disk" not in vm.text
        finally:
            vm.stop()
        assert protected.stat().st_blocks == before
        with protected.open("rb") as f:
            for offset in locations:
                f.seek(offset)
                assert f.read(len(marker)) == marker
        print("PASS: 512 GiB target was recognized and existing data prevented installation", flush=True)
        protected.unlink()

        image = work / "installed.img"
        install(args.mayos, digest(args.mayos), image)
        with image.open("r+b") as f:
            f.truncate(3 * TIB)
        subprocess.run(["sfdisk", "--relocate", "gpt-bak-std", str(image)], check=True)
        with image.open("rb") as f:
            f.seek(512 + 32)
            assert int.from_bytes(f.read(8), "little") == 3 * TIB // 512 - 1
            f.seek(-512, 2)
            assert f.read(8) == b"EFI PART"
        cloned, _, _, _, _ = clone_installed(image, work)
        assert cloned.stat().st_size == 3 * TIB and cloned.stat().st_blocks * 512 < 128 * 1024 ** 2
        print("PASS: 3 TiB installed image retained a valid high-LBA backup GPT and sparse copy", flush=True)
        image.unlink()

        vm = start("large-nvme-cold", cloned, "nvme")
        try:
            vm.wait("MayOS: installed disk matched on NVMe")
            vm.wait("MayOS: first boot setup ready")
            complete_setup(vm)
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            vm.keyboard_command("write large.txt survived")
            vm.wait("\nOK\n")
        finally:
            vm.stop()
        vm = start("large-ahci-cold", cloned, "ahci")
        try:
            vm.wait("MayOS: installed disk matched on AHCI")
            vm.wait("MayOS: desktop ready")
            vm.key("f1")
            vm.keyboard_command("cat large.txt")
            vm.wait("\nsurvived\n")
        finally:
            vm.stop()
        print("PASS: 3 TiB image cold-booted on NVMe and SATA/AHCI and retained its file", flush=True)
        cloned.unlink()

        if args.full_install:
            blank = work / "large-blank.img"
            with blank.open("xb") as f:
                f.truncate(129 * GIB)
            vm = start("large-full-install", blank, "nvme", args.installer.resolve(), timeout=720)
            try:
                install_command(vm)
                vm.wait("MayOS installation complete.")
            finally:
                vm.stop()
            subprocess.run(["sfdisk", "--verify", str(blank)], check=True)
            print("PASS: 129 GiB blank NVMe completed the full scan, installation, and readback", flush=True)


if __name__ == "__main__":
    main()

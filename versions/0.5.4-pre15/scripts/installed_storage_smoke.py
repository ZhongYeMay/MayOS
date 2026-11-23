#!/usr/bin/env python3
"""Install into disposable QEMU SATA and NVMe disks and cold-boot with both present."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from dual_boot_smoke import BUILD, BootVM
from install_mayos import DISK_BYTES
from live_upgrade import digest

MIB = 1024 * 1024


def blank(path):
    with path.open("xb") as image:
        image.truncate(DISK_BYTES + 16 * MIB)


def complete_setup(vm):
    for key, stage in [
        ("ret", "language ready"), ("ret", "license ready"), ("y", "profile ready"),
        ("ret", "updates ready"), ("ret", "appearance ready"),
        ("ret", "review ready"), ("ret", "setup completed"),
    ]:
        vm.key(key)
        vm.wait("MayOS: first boot " + stage)


def case(root, mode, installer, firmware, variables, qemu, two_installs):
    sata = root / "sata.img"
    nvme = root / "nvme.img"
    for disk in (sata, nvme):
        blank(disk)
    target = sata if mode == "AHCI" else nvme
    decoy = nvme if mode == "AHCI" else sata
    device = "/dev/sda" if mode == "AHCI" else "/dev/nvme0n1"
    decoy_digest = digest(decoy)

    def start(stage, media, *, extra_sata=sata, extra_nvme=nvme, prefer_nvme=False):
        tag = f"installed-{mode.lower()}-{stage}"
        vars_path = root / f"{stage}.fd"
        shutil.copyfile(variables, vars_path)
        wrapper = root / f"launch-{stage}"
        wrapper.write_text(f"""#!/usr/bin/env python3
import os
import sys
args = sys.argv[1:]
i = args.index('-drive')
args[i + 1] = {('file=' + str(extra_sata) + ',format=raw,if=none,id=sata-test,discard=unmap,detect-zeroes=unmap')!r}
args += ['-device', {('ide-hd,bus=ide.0,drive=sata-test,bootindex=' + ('2' if prefer_nvme else '1'))!r},
         '-drive', {('file=' + str(extra_nvme) + ',format=raw,if=none,id=nvme-test,discard=unmap,detect-zeroes=unmap')!r},
         '-device', {('nvme,drive=nvme-test,serial=MAYOS-QA,bootindex=' + ('1' if prefer_nvme else '2'))!r}]
if '-cdrom' in args:
    i = args.index('-cdrom')
    image = args[i+1]
    args[i:i+2] = ['-drive', 'file=' + image + ',if=none,media=cdrom,id=installer-media',
                   '-device', 'ide-cd,bus=ide.1,drive=installer-media,bootindex=0']
os.execv({qemu!r}, [{qemu!r}, *args])
""")
        wrapper.chmod(0o755)
        return BootVM(iso=media, disk=extra_sata, tag=tag, qemu=str(wrapper), memory=512,
                      timeout=65, machine="q35", ovmf_code=firmware, ovmf_vars=vars_path)

    def install_to(stage, destination):
        vm = start(stage, installer)
        try:
            vm.wait("Install MayOS (serial console)")
            vm.send_serial("\x1b[B\r")
            vm.wait("Enter the target path")
            vm.send_serial(destination + "\n")
            vm.wait(f"To continue, type INSTALL {destination}")
            vm.send_serial(f"INSTALL {destination}\n")
            vm.wait("MayOS installation complete.")
        finally:
            vm.stop()

    install_to("install", device)
    assert digest(decoy) == decoy_digest
    print(f"PASS: installed to {mode} while the other disk remained byte-identical", flush=True)

    selected = f"MayOS: installed disk matched on {mode}"
    vm = start("first", None)
    try:
        vm.wait(selected)
        vm.wait("MayOS: first boot setup ready")
        complete_setup(vm)
        vm.wait("MayOS: desktop ready")
        vm.key("f1")
        vm.keyboard_command("write keep.txt installed-storage")
        vm.wait("\nOK\n")
    finally:
        vm.stop()
    vm = start("cold", None)
    try:
        vm.wait(selected)
        vm.wait("MayOS: desktop ready")
        vm.key("f1")
        vm.keyboard_command("cat keep.txt")
        vm.wait("\ninstalled-storage\n")
    finally:
        vm.stop()
    assert digest(decoy) == decoy_digest
    print(f"PASS: {mode} installed system cold-booted twice without ISO and retained its file", flush=True)

    if mode == "AHCI":
        duplicate = root / "duplicate.img"
        subprocess.run(["cp", "--sparse=always", str(target), str(duplicate)], check=True)
        duplicate_baseline = digest(target)
        vm = start("duplicate", None, extra_nvme=duplicate)
        try:
            vm.wait("MayOS: installed disk identity is ambiguous; storage stays in RAM")
            vm.wait("MayOS: desktop ready")
            assert "MayOS: MayFS GPT partition selected" not in vm.text
        finally:
            vm.stop()
        assert digest(target) == digest(duplicate) == duplicate_baseline
        print("PASS: cloned GPT disk identity fell back to RAM with neither drive mounted", flush=True)
        duplicate.unlink()

        if two_installs:
            before = digest(sata)
            install_to("install-second", "/dev/nvme0n1")
            assert digest(sata) == before

            def identities(disk):
                layout = json.loads(subprocess.check_output(["sfdisk", "--json", str(disk)]))["partitiontable"]
                return {layout["id"], *(entry["uuid"] for entry in layout["partitions"])}
            sata_guids, nvme_guids = identities(sata), identities(nvme)
            assert len(sata_guids) == len(nvme_guids) == 3
            assert sata_guids.isdisjoint(nvme_guids)
            print("PASS: two installations from one ISO have six distinct GPT identities", flush=True)

            vm = start("second-setup", None, prefer_nvme=True)
            try:
                vm.wait("MayOS: installed disk matched on NVMe")
                vm.wait("MayOS: first boot setup ready")
                complete_setup(vm)
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                vm.keyboard_command("write keep.txt nvme-separate")
                vm.wait("\nOK\n")
            finally:
                vm.stop()
            assert digest(sata) == before

            for mode, value in [("AHCI", "installed-storage"), ("NVMe", "nvme-separate")]:
                vm = start("both-" + mode.lower(), None, prefer_nvme=mode == "NVMe")
                try:
                    vm.wait(f"MayOS: installed disk matched on {mode}")
                    vm.wait("MayOS: desktop ready")
                    vm.key("f1")
                    vm.keyboard_command("cat keep.txt")
                    vm.wait(f"\n{value}\n")
                finally:
                    vm.stop()
            assert digest(sata) == before
            print("PASS: with both installations attached, each firmware boot mounted only its own MayFS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", type=Path, default=BUILD / "mayos-installer.iso")
    parser.add_argument("--ovmf-code", type=Path, required=True)
    parser.add_argument("--ovmf-vars", type=Path, required=True)
    parser.add_argument("--with-two-installs", action="store_true",
                        help="also install both drives from the same ISO and boot each")
    args = parser.parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        parser.error("qemu-system-x86_64 is required")
    for mode in ("AHCI", "NVMe"):
        with tempfile.TemporaryDirectory(prefix=f"mayos-installed-{mode.lower()}-") as folder:
            case(Path(folder), mode, args.installer.resolve(), args.ovmf_code.resolve(),
                 args.ovmf_vars.resolve(), qemu, args.with_two_installs)


if __name__ == "__main__":
    main()

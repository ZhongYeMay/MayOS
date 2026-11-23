#!/usr/bin/env python3
"""Install onto disposable QEMU disks and cold-boot with both present."""
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


TOPOLOGY = {
    "mixed": (("AHCI", "/dev/sda"), ("NVMe", "/dev/nvme0n1")),
    "sata": (("AHCI", "/dev/sda"), ("AHCI", "/dev/sdb")),
    "nvme": (("NVMe", "/dev/nvme0n1"), ("NVMe", "/dev/nvme1n1")),
    "namespaces": (("NVMe", "/dev/nvme0n1"), ("NVMe", "/dev/nvme0n2")),
}


def boot_esp_once(vars_path, disk):
    # OVMF bootindex orders the controller but can still start its first
    # namespace. Select the target ESP through a standard UEFI BootNext entry.
    from virt.firmware.efi.devpath import DevicePath, DevicePathElem
    from virt.firmware.varstore.edk2 import Edk2VarStore

    layout = json.loads(subprocess.check_output(["sfdisk", "--json", str(disk)]))["partitiontable"]
    esp = next(entry for entry in layout["partitions"]
               if entry["type"].lower() == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
    partition = DevicePathElem()
    partition.set_gpt(1, esp["start"], esp["size"], esp["uuid"])
    loader = DevicePathElem()
    loader.set_filepath(r"\EFI\BOOT\BOOTX64.EFI")
    path = DevicePath(bytes(partition) + bytes(loader))
    store = Edk2VarStore(str(vars_path))
    entries = store.get_varlist()
    entries.set_boot_next(entries.add_boot_entry("MayOS storage regression", path))
    store.write_varstore(str(vars_path), entries)


def case(root, topology, installer, firmware, variables, qemu, two_installs, target_index=0):
    targets = TOPOLOGY[topology]
    first = root / "first.img"
    peer = root / "peer.img"
    disks = (first, peer)
    for disk in disks:
        blank(disk)
    mode, device = targets[target_index]
    target, decoy = disks[target_index], disks[1 - target_index]
    decoy_digest = digest(decoy)

    def start(stage, media, *, peer_disk=peer, prefer_peer=bool(target_index)):
        label = "ns" if topology == "namespaces" else topology
        tag = f"installed-{label}-{target_index}-{stage}"
        priorities = (2, 1) if prefer_peer else (1, 2)
        if topology == "namespaces":
            devices = [
                "-device", "nvme,id=shared-nvme,serial=MAYOS-QA-SHARED",
                "-device", f"nvme-ns,bus=shared-nvme,drive=first-test,nsid=1,bootindex={priorities[0]}",
                "-device", f"nvme-ns,bus=shared-nvme,drive=peer-test,nsid=7,bootindex={priorities[1]}",
            ]
        else:
            ports = ["ide.0", "ide.2"]
            devices = []
            for i, ((backend, _), drive) in enumerate(zip(targets, ("first-test", "peer-test"))):
                if backend == "AHCI":
                    spec = f"ide-hd,bus={ports[i]},drive={drive}"
                else:
                    spec = f"nvme,drive={drive},serial=MAYOS-QA-{i}"
                devices += ["-device", f"{spec},bootindex={priorities[i]}"]
        vars_path = root / f"{stage}.fd"
        shutil.copyfile(variables, vars_path)
        if topology == "namespaces" and media is None and prefer_peer:
            boot_esp_once(vars_path, peer_disk)
        wrapper = root / f"launch-{stage}"
        wrapper.write_text(f"""#!/usr/bin/env python3
import os
import sys
args = sys.argv[1:]
i = args.index('-drive')
args[i + 1] = {('file=' + str(first) + ',format=raw,if=none,id=first-test,discard=unmap,detect-zeroes=unmap')!r}
args += ['-drive', {('file=' + str(peer_disk) + ',format=raw,if=none,id=peer-test,discard=unmap,detect-zeroes=unmap')!r}]
args += {devices!r}
if '-cdrom' in args:
    i = args.index('-cdrom')
    image = args[i+1]
    args[i:i+2] = ['-drive', 'file=' + image + ',if=none,media=cdrom,id=installer-media',
                   '-device', 'ide-cd,bus=ide.1,drive=installer-media,bootindex=0']
os.execv({qemu!r}, [{qemu!r}, *args])
""")
        wrapper.chmod(0o755)
        return BootVM(iso=media, disk=first, tag=tag, qemu=str(wrapper), memory=512,
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

    if target_index == 0:
        duplicate = root / "duplicate.img"
        subprocess.run(["cp", "--sparse=always", str(target), str(duplicate)], check=True)
        duplicate_baseline = digest(target)
        vm = start("duplicate", None, peer_disk=duplicate)
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
            before = digest(first)
            peer_mode, peer_device = targets[1]
            install_to("install-second", peer_device)
            assert digest(first) == before

            def identities(disk):
                layout = json.loads(subprocess.check_output(["sfdisk", "--json", str(disk)]))["partitiontable"]
                return {layout["id"], *(entry["uuid"] for entry in layout["partitions"])}
            first_guids, peer_guids = identities(first), identities(peer)
            assert len(first_guids) == len(peer_guids) == 3
            assert first_guids.isdisjoint(peer_guids)
            print("PASS: two installations from one ISO have six distinct GPT identities", flush=True)

            vm = start("second-setup", None, prefer_peer=True)
            try:
                vm.wait(f"MayOS: installed disk matched on {peer_mode}")
                vm.wait("MayOS: first boot setup ready")
                complete_setup(vm)
                vm.wait("MayOS: desktop ready")
                vm.key("f1")
                vm.keyboard_command("write keep.txt peer-separate")
                vm.wait("\nOK\n")
            finally:
                vm.stop()
            assert digest(first) == before

            for use_peer, value in [(False, "installed-storage"), (True, "peer-separate")]:
                boot_mode = peer_mode if use_peer else mode
                vm = start("both-peer" if use_peer else "both-first", None, prefer_peer=use_peer)
                try:
                    vm.wait(f"MayOS: installed disk matched on {boot_mode}")
                    vm.wait("MayOS: desktop ready")
                    vm.key("f1")
                    vm.keyboard_command("cat keep.txt")
                    vm.wait(f"\n{value}\n")
                finally:
                    vm.stop()
            assert digest(first) == before
            print(f"PASS: {topology} firmware boots each mounted only their own MayFS", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", type=Path, default=BUILD / "mayos-installer.iso")
    parser.add_argument("--ovmf-code", type=Path, required=True)
    parser.add_argument("--ovmf-vars", type=Path, required=True)
    parser.add_argument("--with-two-installs", action="store_true",
                        help="also install both drives from the same ISO and boot each")
    topologies = parser.add_mutually_exclusive_group()
    topologies.add_argument("--same-sata", dest="topology", action="store_const", const="sata")
    topologies.add_argument("--same-nvme", dest="topology", action="store_const", const="nvme",
                            help="install and boot both drives on independent NVMe controllers")
    topologies.add_argument("--shared-nvme", dest="topology", action="store_const", const="namespaces",
                            help="install and boot two namespaces on one NVMe controller")
    args = parser.parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        parser.error("qemu-system-x86_64 is required")
    topology = args.topology or "mixed"
    if topology == "namespaces":
        try:
            from virt.firmware.varstore import edk2
        except ImportError:
            parser.error("--shared-nvme requires the virt-firmware Python package")
    for index in (0,) if args.topology else (0, 1):
        with tempfile.TemporaryDirectory(prefix=f"mayos-installed-{topology}-") as folder:
            case(Path(folder), topology, args.installer.resolve(), args.ovmf_code.resolve(),
                 args.ovmf_vars.resolve(), qemu, args.with_two_installs or args.topology is not None, index)


if __name__ == "__main__":
    main()

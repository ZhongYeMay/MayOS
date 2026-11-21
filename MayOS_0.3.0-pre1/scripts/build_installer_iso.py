#!/usr/bin/env python3
"""Build an offline bootable installer ISO from local Linux tools."""
import argparse
import gzip
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile

from dual_iso import BUILD, ROOT, bootable_iso
from install_mayos import install
from linux import cpio_member, validate_busybox
from live_upgrade import digest


def modules(version):
    included = set()
    for driver in ("ata_piix", "ahci", "sd_mod", "nvme", "virtio_pci", "virtio_blk"):
        result = subprocess.run(
            ["modprobe", "--set-version", version, "--show-depends", driver],
            capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            if line.startswith("builtin "):
                continue
            if not line.startswith("insmod "):
                raise ValueError(f"Unsupported module instruction: {line}")
            path = Path(line.split()[1])
            if path not in included:
                included.add(path)
                yield path


def module_bytes(path):
    if path.suffix == ".zst":
        return subprocess.check_output(["zstd", "-dc", str(path)])
    if path.suffix == ".xz":
        return subprocess.check_output(["xz", "-dc", str(path)])
    if path.suffix == ".gz":
        return gzip.decompress(path.read_bytes())
    return path.read_bytes()


def initramfs(output, payload, busybox, version, writer):
    applets, _ = validate_busybox(busybox)
    required = {"ash", "mount", "sha256sum", "insmod", "sleep", "poweroff"}
    if not required <= set(applets):
        raise ValueError(f"BusyBox lacks required installer applets: {required - set(applets)}")
    with output.open("wb") as compressed, tempfile.TemporaryFile() as archive:
        number = 0

        def add(name, mode, data=b""):
            nonlocal number
            number += 1
            cpio_member(archive, number, name, mode, data)

        for name in ("bin", "dev", "proc", "sys", "installer", "installer/modules"):
            add(name, stat.S_IFDIR | 0o755)
        add("bin/busybox", stat.S_IFREG | 0o755, busybox.read_bytes())
        add("init", stat.S_IFREG | 0o755, (ROOT / "linux/installer-init").read_bytes())
        add("installer/disk.img", stat.S_IFREG | 0o644, payload.read_bytes())
        add("installer/payload.sha256", stat.S_IFREG | 0o644,
            f"{digest(payload)}  /installer/disk.img\n".encode())
        add("installer/write-disk", stat.S_IFREG | 0o755, writer.read_bytes())
        script = []
        for index, path in enumerate(modules(version)):
            member = f"installer/modules/{index}.ko"
            add(member, stat.S_IFREG | 0o644, module_bytes(path))
            script.append(f"insmod /{member}")
        add("installer/modules.sh", stat.S_IFREG | 0o644, ("\n".join(script) + "\n").encode())
        add("TRAILER!!!", 0)
        archive.seek(0)
        with gzip.GzipFile(fileobj=compressed, mode="wb", compresslevel=4, mtime=0) as zipped:
            shutil.copyfileobj(archive, zipped)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--kernel-version", required=True)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--busybox", default=Path("/bin/busybox"), type=Path)
    parser.add_argument("--output", default=BUILD / "mayos-installer.iso", type=Path)
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-installer-iso-") as directory:
        work = Path(directory)
        stage = work / "iso"
        boot = stage / "boot"
        (boot / "grub").mkdir(parents=True)
        disk = work / "disk.img"
        install(args.iso, args.sha256, disk)
        writer = work / "write-disk"
        subprocess.run(["gcc", "-O2", "-static", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "linux/installer-write-disk.c"), "-o", str(writer)], check=True)
        initramfs(boot / "installer.cpio.gz", disk, args.busybox, args.kernel_version, writer)
        shutil.copyfile(args.kernel, boot / "installer-linux")
        (boot / "grub/grub.cfg").write_text(
            "serial --unit=0 --speed=115200\n"
            "terminal_input console serial\nterminal_output console serial\n"
            "set timeout=8\nset default=0\n"
            "menuentry 'Install MayOS' {\n"
            "  linux /boot/installer-linux console=tty0 quiet\n"
            "  initrd /boot/installer.cpio.gz\n}\n"
            "menuentry 'Install MayOS (serial console)' {\n"
            "  linux /boot/installer-linux console=ttyS0,115200 quiet\n"
            "  initrd /boot/installer.cpio.gz\n}\n")
        grub = argparse.Namespace(firmware="hybrid", grub_mkrescue="grub-mkrescue",
                                  grub_mkstandalone="grub-mkstandalone", grub_file="grub-file",
                                  grub_pc_dir=None, grub_efi_dir=None)
        bootable_iso(stage, args.output.resolve(), grub,
                     efi_search_file="/boot/installer.cpio.gz")


if __name__ == "__main__":
    main()

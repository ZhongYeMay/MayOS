#!/usr/bin/env python3
"""Build the offline UEFI installer from a packaged Firefox-free Linux64 system."""
import argparse
import gzip
import io
import lzma
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

from dual_iso import ROOT, sha256, stage_menu
from linux import cpio_member

MODULES = (
    "loop", "isofs", "squashfs", "crc32c_generic", "ext4", "vfat", "nls_cp437", "nls_iso8859-1",
    "ata_piix", "ahci", "nvme", "vmd", "virtio_pci", "virtio_blk", "virtio_scsi",
    "sd_mod", "sr_mod", "xhci_pci", "ehci_pci", "uhci_hcd", "usb_storage", "uas",
    "usbhid", "hid_generic", "atkbd", "bochs", "virtio_gpu", "evdev",
)


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def dump(image, source, destination):
    run("debugfs", "-R", f'dump "{source}" "{destination}"', image,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not destination.is_file() or not destination.stat().st_size:
        raise ValueError(f"System image is missing {source}")


def initramfs(image, modules, busybox, install, output, work):
    dependency_file = work / "modules.dep"
    dump(image, f"{modules}/modules.dep", dependency_file)
    dependencies = dict(line.split(":", 1) for line in dependency_file.read_text().splitlines())
    lookup = {Path(path).name.split(".ko", 1)[0].replace("-", "_"): path for path in dependencies}
    ordered = {}

    def include(path):
        if path in ordered:
            return
        for dependency in dependencies[path].split():
            include(dependency)
        ordered[path] = None

    for name in MODULES:
        if name in lookup:
            include(lookup[name])
    streams = [("bin", stat.S_IFDIR | 0o755, b""),
               ("modules", stat.S_IFDIR | 0o755, b""),
               ("bin/busybox", stat.S_IFREG | 0o755, busybox.read_bytes()),
               ("init", stat.S_IFREG | 0o755, install.read_bytes())]
    for name in ("dev", "proc", "sys"):
        streams.append((name, stat.S_IFDIR | 0o755, b""))
    order = []
    for index, path in enumerate(ordered, 1):
        decompressed_name = f"{index:03d}.ko"
        temporary = work / Path(path).name
        dump(image, f"{modules}/{path}", temporary)
        data = temporary.read_bytes()
        if path.endswith(".xz"):
            data = lzma.decompress(data)
        elif path.endswith(".zst"):
            data = subprocess.check_output(["zstd", "-dc", temporary])
        streams.append((f"modules/{decompressed_name}", stat.S_IFREG | 0o644, data))
        order.append(decompressed_name)
    streams.append(("modules/order", stat.S_IFREG | 0o644, ("\n".join(order) + "\n").encode()))
    archive = io.BytesIO()
    for inode, (name, mode, data) in enumerate(streams, 1):
        cpio_member(archive, inode, name, mode, data)
    cpio_member(archive, len(streams) + 1, "TRAILER!!!", 0)
    output.write_bytes(gzip.compress(archive.getbuffer(), compresslevel=3, mtime=0))


def grub_loader(work, config, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    embedded = work / "embedded.cfg"
    embedded.write_text(config)
    run("grub-script-check", embedded)
    run("grub-mkstandalone", "-O", "x86_64-efi", "--fonts=", "--locales=", "--themes=",
        "--modules=part_gpt fat iso9660 ext2 search search_fs_file search_fs_uuid regexp loadenv linux serial all_video gfxterm gfxmenu png cpuid test",
        "-o", path, f"boot/grub/grub.cfg={embedded}")
    run("grub-file", "--is-x86_64-efi", path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root-image", type=Path, required=True, help="fresh 1.5 GiB ext4 image from package-root")
    parser.add_argument("--busybox", type=Path, required=True, help="static x86-64 BusyBox")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.root_image.resolve()
    output = args.output.resolve()
    if output.exists() or output.with_suffix(".iso.sha256").exists():
        parser.error("choose a new output file")
    if source.stat().st_size != 1536 * 1024 * 1024:
        parser.error("root image must be the 1.5 GiB Linux64 ext4 image")
    overlay = ROOT / "linux" / "installed"
    with tempfile.TemporaryDirectory(prefix=".mayos-iso-", dir=output.parent) as tmp:
        work = Path(tmp)
        stage = work / "stage"
        grub = stage / "boot" / "grub"
        grub.mkdir(parents=True)
        release_file = work / "release"
        dump(source, "/etc/mayos-linux-release", release_file)
        release = release_file.read_text().strip()
        if release != (ROOT / "VERSION").read_text().strip():
            raise ValueError("system image version does not match VERSION")
        status = work / "dpkg-status"
        dump(source, "/var/lib/dpkg/status", status)
        forbidden = re.search(r"^Package: firefox(?:-esr)?$", status.read_text(), re.MULTILINE)
        if forbidden or "File not found" not in subprocess.run(
                ["debugfs", "-R", "stat /usr/lib/firefox-esr", source], text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout:
            raise ValueError("root image contains Firefox")
        module_list = subprocess.check_output(["debugfs", "-R", "ls -p /usr/lib/modules", source],
                                              text=True, stderr=subprocess.DEVNULL)
        kernels = [entry.split("/")[5] for entry in module_list.splitlines() if "amd64" in entry]
        if len(kernels) != 1:
            raise ValueError("system image must contain one x86-64 kernel module tree")
        boot = stage / "boot"
        dump(source, "/boot/mayos-linux", boot / "mayos-linux")
        initramfs(source, f"/usr/lib/modules/{kernels[0]}", args.busybox.resolve(),
                  overlay / "iso-init", boot / "mayos-installer-initrd", work)
        stage_menu(grub)
        (stage / "MAYOS_LINUX64_INSTALLER").write_text(release + "\n")
        (stage / "root.sha256").write_text(sha256(source) + "\n")
        shutil.copy2(overlay / "disk.py", stage / "disk.py")
        shutil.copy2(overlay / "iso-install", stage / "install")
        shutil.copy2(ROOT / "docs/third-party/dual-boot-licenses/busybox-static-packaging-copyright.txt",
                     stage / "licenses")
        print("Compressing the Firefox-free system image", flush=True)
        if source.stat().st_dev == work.stat().st_dev:
            os.link(source, work / "root.img")
        else:
            run("cp", "--sparse=always", source, work / "root.img")
        run("mksquashfs", work / "root.img", stage / "system.squashfs",
            "-noappend", "-one-file-system", "-all-root", "-comp", "zstd", "-processors", "4",
            "-b", "256K", "-no-progress")
        (grub / "grub.cfg").write_text(
            "source /boot/grub/menu.cfg\n"
            "set gfxpayload=keep\n"
            "menuentry 'Install MayOS Linux64' {\n"
            "    linux /boot/mayos-linux quiet loglevel=3 console=tty0\n"
            "    initrd /boot/mayos-installer-initrd\n"
            "}\n"
            "menuentry 'Install MayOS Linux64 (serial console)' {\n"
            "    linux /boot/mayos-linux quiet loglevel=3 console=tty0 console=ttyS0,115200\n"
            "    initrd /boot/mayos-installer-initrd\n"
            "}\n")
        run("grub-script-check", grub / "grub.cfg")
        loader = stage / "EFI" / "BOOT" / "BOOTX64.EFI"
        grub_loader(work, "search --no-floppy --file --set=root /MAYOS_LINUX64_INSTALLER\n"
                    "configfile /boot/grub/grub.cfg\n", loader)
        grub_loader(work, "regexp --set=1:root '^\\(([^)]+)\\)/' \"$cmdpath\"\n"
                    "configfile /mayos-linux/grub.cfg\n", stage / "disk-efi/BOOT/BOOTX64.EFI")
        fat = grub / "efi.img"
        run("mformat", "-C", "-T", "32768", "-i", fat, "::")
        run("mmd", "-i", fat, "::/EFI", "::/EFI/BOOT")
        run("mcopy", "-i", fat, loader, "::/EFI/BOOT/BOOTX64.EFI")
        candidate = work / "installer.iso"
        run("xorriso", "-as", "mkisofs", "-R", "-V", "MAYOS_LINUX64_INSTALLER",
            "-e", "boot/grub/efi.img", "-no-emul-boot", "-efi-boot-part", "--efi-boot-image",
            "--protective-msdos-label", "-o", candidate, stage)
        candidate.rename(output)
    output.with_suffix(".iso.sha256").write_text(f"{sha256(output)}  {output.name}\n")
    print(output, flush=True)


if __name__ == "__main__":
    main()

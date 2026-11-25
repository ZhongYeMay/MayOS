#!/usr/bin/env python3
"""Build an offline bootable installer ISO from local Linux tools."""
import argparse
import gzip
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile

from dual_iso import BUILD, ROOT, bootable_iso, stage_menu
from install_mayos import ESP_START, SECTOR, SECTOR_4K, install
from linux import cpio_member, validate_busybox
from live_upgrade import digest


def modules(version):
    included = set()
    for driver in ("ata_piix", "ahci", "sd_mod", "nvme", "vfat", "nls_cp437",
                   "nls_iso8859-1", "evdev", "psmouse", "xhci_pci", "usbhid", "hid_generic"):
        result = subprocess.run(
            ["modprobe", "--set-version", version, "--show-depends", driver],
            capture_output=True, text=True)
        if result.returncode:
            raise ValueError(f"Cannot resolve installer driver {driver}: {result.stderr.strip()}")
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


def compact_4kn_payload(disk, payload):
    # Keep the actual prefix and backup GPT. The installer verifies that
    # the gap on the destination is already zero before writing either.
    backup_size = 128 * 128 + SECTOR_4K
    limit = disk.stat().st_size - backup_size
    with disk.open("rb") as source:
        end = limit
        while end:
            start = max(0, end - 1024 * 1024)
            source.seek(start)
            data = source.read(end - start).rstrip(b"\0")
            if data:
                end = start + (len(data) + SECTOR_4K - 1) // SECTOR_4K * SECTOR_4K
                break
            end = start
        if not 1024 * 1024 < end <= 64 * 1024 * 1024:
            raise ValueError("4Kn template prefix exceeds installer staging limits")
        source.seek(0)
        with payload.open("xb") as output:
            output.write(source.read(end))
            source.seek(limit)
            output.write(source.read(backup_size))


def initramfs(output, payloads, busybox, version, writer, gui, upgrade_assets):
    applets, _ = validate_busybox(busybox)
    required = {"sh", "cat", "mount", "umount", "sha256sum", "insmod", "sleep",
                "poweroff", "grep", "cp", "mv", "sync", "tr"}
    if not required <= set(applets):
        raise ValueError(f"BusyBox lacks required installer applets: {required - set(applets)}")
    with output.open("wb") as compressed, gzip.GzipFile(
            fileobj=compressed, mode="wb", compresslevel=4, mtime=0) as archive:
        number = 0

        def add(name, mode, data=b""):
            nonlocal number
            number += 1
            cpio_member(archive, number, name, mode, data)

        for name in ("bin", "dev", "proc", "sys", "target", "installer", "installer/modules"):
            add(name, stat.S_IFDIR | 0o755)
        add("bin/busybox", stat.S_IFREG | 0o755, busybox.read_bytes())
        for applet in sorted(required):
            add(f"bin/{applet}", stat.S_IFLNK | 0o777, b"busybox")
        add("init", stat.S_IFREG | 0o755, (ROOT / "linux/installer-init").read_bytes())
        for payload, checksum in payloads:
            member = f"installer/{payload.name}"
            add(member, stat.S_IFREG | 0o644, payload.read_bytes())
            add(f"installer/{checksum}", stat.S_IFREG | 0o644,
                f"{digest(payload)}  /{member}\n".encode())
        add("installer/write-disk", stat.S_IFREG | 0o755, writer.read_bytes())
        add("installer/gui", stat.S_IFREG | 0o755, gui.read_bytes())
        for asset in upgrade_assets:
            add(f"installer/{asset.name}", stat.S_IFREG | 0o644, asset.read_bytes())
        script = []
        for index, path in enumerate(modules(version)):
            member = f"installer/modules/{index}.ko"
            add(member, stat.S_IFREG | 0o644, module_bytes(path))
            script.append(f"insmod /{member}")
        add("installer/modules.sh", stat.S_IFREG | 0o644, ("\n".join(script) + "\n").encode())
        add("TRAILER!!!", 0)


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
        raw_4k, disk_4k = work / "raw-4k.img", work / "disk-4k.img"
        install(args.iso, args.sha256, raw_4k, sector=SECTOR_4K)
        compact_4kn_payload(raw_4k, disk_4k)
        writer = work / "write-disk"
        subprocess.run(["gcc", "-O2", "-static", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "linux/installer-write-disk.c"), "-o", str(writer)], check=True)
        gui = work / "gui"
        subprocess.run(["gcc", "-O2", "-static", "-Wall", "-Wextra", "-Werror",
                        "-DMAYOS_LINUX", "-I" + str(ROOT / "kernel"), "-I" + str(BUILD),
                        str(ROOT / "linux/installer-ui.c"), "-o", str(gui)], check=True)
        update = work / "update.elf"
        subprocess.run(["mcopy", "-i", f"{disk}@@{ESP_START * SECTOR}",
                        "::/slots/A.elf", str(update)], check=True)
        update_hash = work / "update.sha256"
        update_hash.write_text(f"{digest(update)}  /installer/update.elf\n")
        assets = [update, update_hash]
        for name, assignment in (("active-A.env", "active_slot=A"),
                                 ("active-B.env", "active_slot=B"),
                                 ("trial-A.env", "trial_slot=A"),
                                 ("trial-B.env", "trial_slot=B"),
                                 ("trial-clear.env", None)):
            env = work / name
            subprocess.run(["grub-editenv", str(env), "create"], check=True)
            if assignment:
                subprocess.run(["grub-editenv", str(env), "set", assignment], check=True)
            assets.append(env)
        payloads = [(disk, "payload.sha256"), (disk_4k, "payload-4k.sha256")]
        initramfs(boot / "installer.cpio.gz", payloads, args.busybox,
                  args.kernel_version, writer, gui, assets)
        shutil.copyfile(args.kernel, boot / "installer-linux")
        stage_menu(boot / "grub")
        (boot / "grub/grub.cfg").write_text(
            "serial --unit=0 --speed=115200\n"
            "terminal_input console serial\nterminal_output console serial\n"
            "set menu_color_normal=light-gray/black\n"
            "set menu_color_highlight=black/light-gray\n"
            "set timeout=8\nset default=0\n"
            "menuentry 'Install MayOS' {\n"
            "  insmod all_video\n"
            "  set gfxpayload=1280x720x32,1024x768x32,auto\n"
            "  linux /boot/installer-linux console=tty0 quiet nomodeset mayos_gui\n"
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

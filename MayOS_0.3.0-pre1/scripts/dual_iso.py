#!/usr/bin/env python3
"""Create a BIOS + x86_64 UEFI GRUB ISO with independent kernel entries.

Existing Linux payloads are checked before reuse, so rebuilding an ISO can be
done offline. `make linux-update` explicitly fetches the latest stable release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
LINUX = BUILD / "linux"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def checked_payloads():
    """Do not package an old or corrupt kernel under a new version label."""
    manifest = json.loads((LINUX / "manifest.json").read_text())
    if manifest.get("schema_version") != 1 or manifest.get("architecture") != "x86_64":
        raise ValueError("unsupported Linux manifest; run make linux")
    version = manifest.get("version", "")
    if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", version):
        raise ValueError("Linux manifest does not name a stable release")
    if manifest.get("kernel_release") != version:
        raise ValueError("Linux release does not match its menu version")
    for name, filename in (("bzImage", "bzImage"),
                           ("initramfs", "initramfs.cpio.gz"),
                           ("config", "kernel.config")):
        item = manifest["artifacts"][name]
        path = LINUX / filename
        if item["path"] != filename or not path.is_file():
            raise ValueError(f"missing Linux payload {filename}; run make linux")
        if sha256(path) != item["sha256"]:
            raise ValueError(f"Linux payload checksum mismatch: {filename}; run make linux")
    with (LINUX / "bzImage").open("rb") as handle:
        header = handle.read(0x240)
    if header[0x1FE:0x200] != b"\x55\xaa" or header[0x202:0x206] != b"HdrS":
        raise ValueError("Linux image lacks the x86 boot protocol header")
    if not int.from_bytes(header[0x236:0x238], "little") & 1:
        raise ValueError("Linux image is not a 64-bit x86 kernel")
    return manifest


def grub_directory(explicit, platform, command_directory=None):
    """Resolve modules without requiring an installation outside the workspace."""
    candidates = [Path(explicit)] if explicit else []
    if command_directory:
        directory = Path(command_directory)
        candidates += [directory] if directory.name == platform else []
        candidates += [directory.parent / platform]
    if os.environ.get("MAYOS_TOOL_ROOT"):
        candidates += [Path(os.environ["MAYOS_TOOL_ROOT"]) / "usr/lib/grub" / platform]
    candidates += [Path("/usr/lib/grub") / platform, Path("/usr/lib/grub2") / platform]
    for path in candidates:
        if (path / "modinfo.sh").is_file() and (path / "normal.mod").is_file():
            return path.resolve()
    package = "grub-pc-bin" if platform == "i386-pc" else "grub-efi-amd64-bin"
    raise ValueError(f"missing {platform} GRUB modules; install {package} or set GRUB_{'PC' if platform == 'i386-pc' else 'EFI'}_DIR")


def bootable_iso(stage, output, args):
    """Use GRUB's normal BIOS builder with a separate EFI standalone image.

    grub-mkrescue's -d option accepts one platform, which matters for packages
    unpacked in an unprivileged workspace. The EFI boot image below uses the
    same xorriso options as upstream grub-mkrescue, with an embedded module set
    so its EFI loader never accidentally loads BIOS modules from the ISO.
    """
    command = shlex.split(args.grub_mkrescue)
    if not command:
        raise ValueError("GRUB_MKRESCUE is empty")
    supplied_directory = None
    cleaned = []
    index = 0
    while index < len(command):
        item = command[index]
        if item in ("-d", "--directory"):
            index += 1
            if index >= len(command):
                raise ValueError("GRUB_MKRESCUE has an incomplete directory option")
            supplied_directory = command[index]
        elif item.startswith("--directory="):
            supplied_directory = item.split("=", 1)[1]
        else:
            cleaned.append(item)
        index += 1
    pc = grub_directory(args.grub_pc_dir, "i386-pc", supplied_directory)
    command = cleaned + ["-d", str(pc)]
    platforms = ["BIOS i386-pc"]
    efi_options = []
    efi_hash = None
    if args.firmware == "hybrid":
        efi = grub_directory(args.grub_efi_dir, "x86_64-efi", str(pc))
        loader_dir = stage / "EFI/BOOT"
        loader_dir.mkdir(parents=True)
        loader = loader_dir / "BOOTX64.EFI"
        # Standalone GRUB keeps its prefix on its embedded memdisk. Only root
        # changes to the ISO, so insmod continues using x86_64-efi modules.
        embedded = BUILD / "efi-embedded.cfg"
        embedded.write_text(
            "search --no-floppy --file --set=root /boot/mayos.elf\n"
            "configfile /boot/grub/grub.cfg\n")
        standalone = shlex.split(args.grub_mkstandalone)
        subprocess.run(standalone + ["-O", "x86_64-efi", "-d", str(efi),
                       "--fonts=", "--themes=", "--locales=", "--compress=xz",
                       "-o", str(loader), f"boot/grub/grub.cfg={embedded}"], check=True)
        subprocess.run([args.grub_file, "--is-x86_64-efi", str(loader)], check=True)
        fat = stage / "boot/grub/efi.img"
        # 16 MiB leaves room for the complete standalone module set. mformat
        # chooses the appropriate FAT layout; no loop mounts or root required.
        subprocess.run(["mformat", "-C", "-T", "32768", "-v", "MAYOSEFI",
                        "-i", str(fat), "::"], check=True)
        subprocess.run(["mmd", "-i", str(fat), "::/EFI", "::/EFI/BOOT"], check=True)
        subprocess.run(["mcopy", "-i", str(fat), str(loader), "::/EFI/BOOT/BOOTX64.EFI"], check=True)
        efi_options = ["--efi-boot", "boot/grub/efi.img", "-efi-boot-part", "--efi-boot-image"]
        platforms.append("UEFI x86_64 (Secure Boot disabled)")
        efi_hash = sha256(loader)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        subprocess.run(command + ["-o", str(temporary), str(stage)] + efi_options,
                       cwd=ROOT, check=True)
        # A successful mkrescue exit alone does not prove EFI catalog support.
        report = subprocess.run(["xorriso", "-indev", str(temporary),
                                 "-report_el_torito", "plain"], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout
        if not re.search(r"El Torito boot img\s*:\s*\d+\s+BIOS", report):
            raise ValueError("generated ISO is missing the BIOS boot catalog entry")
        if args.firmware == "hybrid" and not re.search(r"El Torito boot img\s*:\s*\d+\s+UEFI", report):
            raise ValueError("generated ISO is missing the UEFI boot catalog entry")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    output.with_suffix(".iso.sha256").write_text(f"{sha256(output)}  {output.name}\n")
    output.with_suffix(".iso.boot.json").write_text(json.dumps({
        "schema_version": 1, "platforms": platforms,
        "grub_version": subprocess.check_output([command[0], "--version"], text=True).strip(),
        "iso_sha256": sha256(output), "efi_loader_sha256": efi_hash,
        "secure_boot": False, "el_torito_report": report,
    }, indent=2) + "\n")
    print(f"Bootable ISO: {output}\nFirmware: {', '.join(platforms)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="latest")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--busybox", type=Path)
    parser.add_argument("--grub-mkrescue", default=os.environ.get("GRUB_MKRESCUE", "grub-mkrescue"))
    parser.add_argument("--grub-mkstandalone", default=os.environ.get("GRUB_MKSTANDALONE", "grub-mkstandalone"))
    parser.add_argument("--grub-file", default=os.environ.get("GRUB_FILE", "grub-file"))
    parser.add_argument("--grub-pc-dir", default=os.environ.get("GRUB_PC_DIR"))
    parser.add_argument("--grub-efi-dir", default=os.environ.get("GRUB_EFI_DIR"))
    parser.add_argument("--firmware", choices=("hybrid", "bios"), default="hybrid")
    parser.add_argument("--custom-only", action="store_true", help="build mayos.iso without Linux")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.version != "latest" and not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", args.version):
        parser.error("--version must be latest or a stable release number")
    if not (BUILD / "mayos.elf").is_file():
        parser.error("build/mayos.elf is missing; use make dual")

    if args.custom_only:
        stage = BUILD / "iso"
        if stage.exists():
            shutil.rmtree(stage)
        (stage / "boot/grub").mkdir(parents=True)
        shutil.copy2(BUILD / "mayos.elf", stage / "boot/mayos.elf")
        shutil.copy2(ROOT / "boot/grub/grub.cfg", stage / "boot/grub/grub.cfg")
        bootable_iso(stage, BUILD / "mayos.iso", args)
        return

    # status also compares the Linux builder/config/init input hashes.
    status = subprocess.run([sys.executable, str(ROOT / "scripts/linux.py"), "status"],
                            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    manifest = None
    if status.returncode == 0:
        manifest = checked_payloads()
    rebuild = manifest is None or (args.version != "latest" and manifest["version"] != args.version)
    if args.busybox and manifest and sha256(args.busybox) != manifest["busybox"]["sha256"]:
        rebuild = True
    if rebuild:
        command = [sys.executable, str(ROOT / "scripts/linux.py"), "build",
                   "--version", args.version, "--jobs", str(args.jobs)]
        if args.busybox:
            command += ["--busybox", str(args.busybox.resolve())]
        subprocess.run(command, cwd=ROOT, check=True)
        manifest = checked_payloads()

    version = manifest["version"]
    stage = BUILD / "dual-iso"
    if stage.exists():
        shutil.rmtree(stage)
    boot = stage / "boot"
    (boot / "grub").mkdir(parents=True)
    (boot / "linux").mkdir()
    shutil.copy2(BUILD / "mayos.elf", boot / "mayos.elf")
    for filename in ("bzImage", "initramfs.cpio.gz", "manifest.json", "kernel.config"):
        shutil.copy2(LINUX / filename, boot / "linux" / filename)
    if (LINUX / "licenses").is_dir():
        shutil.copytree(LINUX / "licenses", stage / "licenses")
    template = (ROOT / "boot/grub/dual.cfg.in").read_text()
    if "@LINUX_VERSION@" not in template:
        raise ValueError("GRUB template must have a Linux version placeholder")
    (boot / "grub/grub.cfg").write_text(template.replace("@LINUX_VERSION@", version))
    output = BUILD / "mayos-dual.iso"
    bootable_iso(stage, output, args)
    print(f"MayOS custom desktop + Linux {version} x86_64 desktop / rescue shell")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"dual ISO build failed: {error}", file=sys.stderr)
        sys.exit(1)

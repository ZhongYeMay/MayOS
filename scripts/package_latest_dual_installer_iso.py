#!/usr/bin/env python3
"""Package an *actual*, single BIOS/UEFI ISO with latest MayKernel, Linux and installer.

Components are independently versioned and cryptographically pinned:
  MayKernel 0.5.2-pre4 (published boot ISO)
  Linux 7.3.1 desktop/rescue (built from active main source)
  Installer/recovery 0.5.2-pre2 (published offline installer ISO)
The installer installs 0.5.2-pre2, NOT the displayed 0.5.2-pre4 live kernel.
This intentionally refuses unknown or altered release components.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ROOT / "versions/0.5.0-pre2-dev"
sys.path.insert(0, str(ACTIVE / "scripts"))
from dual_iso import bootable_iso, checked_payloads, sha256  # noqa: E402

LATEST_KERNEL_RELEASE = "0.5.2-pre4"
INSTALLER_RELEASE = "0.5.2-pre2"
KERNEL_SHA256 = "c51b45c92ea4cd6645b690e433af3f562507fc69ab62d5d39891c10284ac5bb7"
INSTALLER_SHA256 = "c46ee45ded237f0c5823f94e498dad89700f7c54c0d80ba4137e5be3bf572a1a"
EXPECTED_KERNEL_FILE = "MayOS-0.5.2-pre4-MayKernel-x86_64.iso"
EXPECTED_INSTALLER_FILE = "MayOS-0.5.2-pre2-Installer-x86_64.iso"


def require_sha(path: Path, expected: str, kind: str):
    if not path.is_file() or not path.stat().st_size:
        raise ValueError(f"{kind}: missing input {path}")
    actual = sha256(path)
    if actual != expected:
        raise ValueError(f"{kind}: SHA-256 mismatch: got {actual}, expected {expected}")


def extract(iso: Path, original: str, target: Path):
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["xorriso", "-osirrox", "on", "-indev", str(iso),
                    "-extract", original, str(target)],
                   check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.PIPE)
    if not target.is_file() or target.stat().st_size < 8192:
        raise ValueError(f"ISO entry missing/empty: {iso.name}:{original}")


def build(args):
    custom = args.maykernel_iso.resolve()
    installer = args.installer_iso.resolve()
    if custom.name != EXPECTED_KERNEL_FILE or installer.name != EXPECTED_INSTALLER_FILE:
        raise ValueError("Refusing incorrectly version-labelled input ISO")
    require_sha(custom, KERNEL_SHA256, "MayKernel release")
    require_sha(installer, INSTALLER_SHA256, "Installer release")
    manifest = checked_payloads()
    if manifest["version"] != "7.3.1" or manifest["kernel_release"] != "7.3.1":
        raise ValueError("Linux kernel 7.3.1 was not built/validated")
    linux = ACTIVE / "build/linux"
    work = ACTIVE / "build/triple-iso"
    if work.exists():
        shutil.rmtree(work)
    boot = work / "boot"
    (boot / "grub").mkdir(parents=True)
    (boot / "linux").mkdir(parents=True)
    extract(custom, "/boot/mayos.elf", boot / "mayos.elf")
    extract(installer, "/boot/installer-linux", boot / "installer-linux")
    extract(installer, "/boot/installer.cpio.gz", boot / "installer.cpio.gz")
    subprocess.run(["grub-file", "--is-x86-multiboot", str(boot / "mayos.elf")],
                   check=True)
    for name in ("bzImage", "initramfs.cpio.gz", "manifest.json", "kernel.config"):
        shutil.copy2(linux / name, boot / "linux" / name)
    # The original Linux desktop, rescue and compatibility entries stay intact.
    menu = (ACTIVE / "boot/grub/dual.cfg.in").read_text().replace(
        "@LINUX_VERSION@", manifest["kernel_release"])
    hook = "\nelse\nmenuentry"
    if menu.count(hook) != 1:
        raise ValueError("GRUB template changed, cannot inject installer safely")
    installer_entries = """
menuentry "Install MayOS 0.5.2-pre2 (BLANK DISK ONLY)" --id mayos-install {
    set gfxpayload=$gfxmode
    linux /boot/installer-linux console=tty0 quiet nomodeset mayos_gui
    initrd /boot/installer.cpio.gz
    boot
}
menuentry "Install/Recover 0.5.2-pre2 (serial console)" --id mayos-installer-serial {
    set gfxpayload=$gfxmode
    linux /boot/installer-linux console=ttyS0,115200 quiet
    initrd /boot/installer.cpio.gz
    boot
}
"""
    menu = menu.replace(hook, "\n" + installer_entries + hook)
    menu = menu.replace("set timeout=5", "set timeout=12")
    for required in ("Start MayKernel64", "Start Linux64",
                     "Install MayOS 0.5.2-pre2", "mayos-installer-serial"):
        if required not in menu:
            raise ValueError(f"GRUB menu missing {required}")
    (boot / "grub/grub.cfg").write_text(menu)
    shutil.copytree(ACTIVE / "boot/grub/themes", boot / "grub/themes")
    # Use the familiar MayOS GRUB branding without reconstructing GUI frames.
    shutil.copy2(ACTIVE / "assets/limine-mayos-brand.png",
                 boot / "grub/themes/mayos/mayos-brand.png")
    if (linux / "licenses").is_dir():
        shutil.copytree(linux / "licenses", work / "licenses")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    bootable_iso(work, output, argparse.Namespace(
        firmware="hybrid", grub_mkrescue="grub-mkrescue",
        grub_mkstandalone="grub-mkstandalone", grub_file="grub-file",
        grub_pc_dir=None, grub_efi_dir=None))
    if output.stat().st_size <= max(custom.stat().st_size, installer.stat().st_size):
        raise AssertionError("combined ISO suspiciously small")
    record = {
        "schema_version": 1,
        "artifact": output.name,
        "sha256": sha256(output),
        "components": {
            "live_maykernel": {
                "release": LATEST_KERNEL_RELEASE, "source_iso": custom.name,
                "source_sha256": KERNEL_SHA256,
                "kernel_sha256": sha256(boot / "mayos.elf")},
            "linux_desktop_and_rescue": {
                "release": manifest["kernel_release"],
                "source_tree": "versions/0.5.0-pre2-dev",
                "kernel_sha256": sha256(boot / "linux/bzImage"),
                "initramfs_sha256": sha256(boot / "linux/initramfs.cpio.gz")},
            "offline_installer": {
                "release": INSTALLER_RELEASE,
                "installed_system_version": INSTALLER_RELEASE,
                "source_iso": installer.name,
                "source_sha256": INSTALLER_SHA256,
                "kernel_sha256": sha256(boot / "installer-linux"),
                "initramfs_sha256": sha256(boot / "installer.cpio.gz")}},
        "limitations": [
            "Installer installs 0.5.2-pre2, NOT live 0.5.2-pre4.",
            "No production or physical disk compatibility guarantee.",
            "Secure Boot unsupported; test with isolated empty QEMU disks first.",
            "No direct disk installation from the MayKernel preview menu."
        ],
        "source_commit": args.source_commit,
        "boot": ["BIOS", "UEFI x86_64"],
    }
    (output.parent / (output.stem + ".manifest.json")).write_text(
        json.dumps(record, indent=2, ensure_ascii=False) + "\n")
    (output.parent / "SHA256SUMS.txt").write_text(
        f"{sha256(output)}  {output.name}\n")
    print(json.dumps(record, indent=2, ensure_ascii=False), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--maykernel-iso", type=Path, required=True)
    parser.add_argument("--installer-iso", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    if len(args.source_commit) != 40 or any(c not in "0123456789abcdef"
                                            for c in args.source_commit):
        parser.error("source-commit must be a complete Git SHA")
    build(args)


if __name__ == "__main__":
    main()

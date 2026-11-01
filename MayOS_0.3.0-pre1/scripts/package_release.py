#!/usr/bin/env python3
"""Package a tested dual-boot image and its matching third-party sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def add_file(archive, path, name):
    # Upstream source archives are already compressed and dominate the size.
    kind = zipfile.ZIP_STORED if path.suffix in (".xz", ".bz2", ".gz", ".png", ".jpg") else zipfile.ZIP_DEFLATED
    archive.write(path, name, compress_type=kind)


def project_sources(output_dir):
    """Include the entire first-party build, excluding generated payloads."""
    for base, directories, filenames in os.walk(ROOT):
        base = Path(base)
        directories[:] = sorted(directory for directory in directories
            if directory not in ("__pycache__", ".git")
            and not (base == ROOT and directory in ("build", "dist"))
            and (base / directory).resolve() != output_dir.resolve())
        for filename in sorted(filenames):
            path = base / filename
            if path.is_file():
                yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--components-dir", type=Path, required=True,
                        help="BusyBox/musl/GRUB source bundle with provenance.json and SHA256SUMS")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--recordings-dir", type=Path,
                        help="directory containing both final UEFI MP4s and JSON manifests; defaults to output-dir")
    args = parser.parse_args()
    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?", version):
        parser.error("invalid VERSION")
    subprocess.run([sys.executable, str(ROOT / "scripts/linux.py"), "status"],
                   stdout=subprocess.DEVNULL, check=True)
    manifest = json.loads((BUILD / "linux/manifest.json").read_text())
    tests = json.loads((BUILD / "dual-boot-test-results.json").read_text())
    iso_hash = digest(BUILD / "mayos-dual.iso")
    if tests["iso_sha256"] != iso_hash:
        parser.error("dual-boot tests do not describe the current ISO; rerun make test-dual")
    if tests["kernel_release"] != manifest["kernel_release"]:
        parser.error("tested Linux release does not match the payload")
    uefi = json.loads((BUILD / "uefi-test-results.json").read_text())
    if uefi["iso_sha256"] != iso_hash or uefi["kernel_release"] != manifest["kernel_release"]:
        parser.error("UEFI tests do not describe the current ISO; rerun make test-uefi")
    required_bios = {"custom_allocator", "custom_mayfs_cold_boot", "linux_gui_process",
                     "linux_gui_mouse", "linux_gui_editor_ctrl_s", "mayfs_unchanged_by_linux"}
    required_uefi = {"uefi_custom_allocator", "uefi_mayfs_cold_boot", "linux_efi_runtime",
                     "linux_gop_framebuffer", "linux_desktop_process", "uefi_linux_rescue_shell",
                     "uefi_linux_preserves_mayfs"}
    if not required_bios.issubset(tests["tests"]) or not required_uefi.issubset(uefi["tests"]):
        parser.error("missing required BIOS or UEFI runtime checks")
    for report in (tests, uefi):
        if report["mayfs_sha256_before_linux"] != report["mayfs_sha256_after_linux"]:
            parser.error("Linux changed the MayFS test disk")
    boot = json.loads((BUILD / "mayos-dual.iso.boot.json").read_text())
    if boot["iso_sha256"] != iso_hash or not any("UEFI" in p for p in boot["platforms"]):
        parser.error("hybrid boot provenance does not match the tested image")
    recording_dir = args.recordings_dir or args.output_dir
    recording_files = []
    recording_manifests = []
    for kernel, label in (("custom", "MayKernel"), ("linux", "Linux")):
        video = recording_dir / f"MayOS-{version}-UEFI-{label}.mp4"
        evidence = video.with_suffix(".json")
        record = json.loads(evidence.read_text())
        if (record["iso_sha256"] != iso_hash or record["kernel"] != kernel
                or record["firmware"] != "uefi" or record["video_sha256"] != digest(video)):
            parser.error(f"recording evidence does not match the release: {video.name}")
        recording_files.append(video)
        recording_manifests.append(evidence)
    components = args.components_dir.resolve()
    provenance = json.loads((components / "provenance.json").read_text())
    if provenance["busybox"]["binary_sha256"] != manifest["busybox"]["sha256"]:
        parser.error("corresponding BusyBox sources do not match this binary")
    if not {"i386-pc", "x86_64-efi"}.issubset(provenance["grub"].get("platforms", [])):
        parser.error("GRUB provenance must cover BIOS and x64 EFI")
    subprocess.run(["sha256sum", "-c", "SHA256SUMS"], cwd=components,
                   stdout=subprocess.DEVNULL, check=True)
    kernel_archive = (BUILD / "linux" / manifest["source"]["archive"]).resolve()
    if not kernel_archive.is_relative_to((BUILD / "linux/downloads").resolve()):
        parser.error("unsafe Linux source path")
    if digest(kernel_archive) != manifest["source"]["sha256"]:
        parser.error("corresponding Linux source archive hash mismatch")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    name = f"MayOS-{version}"
    source_zip = args.output_dir / f"{name}-third-party-sources.zip"
    source_root = "mayos-third-party-sources/"
    with zipfile.ZipFile(source_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(components.rglob("*")):
            if path.is_file():
                add_file(archive, path, source_root + "components/" + path.relative_to(components).as_posix())
        add_file(archive, kernel_archive, source_root + "linux/" + kernel_archive.name)
        major = manifest["version"].split(".")[0]
        sums = BUILD / "linux/downloads" / f"sha256sums-v{major}.asc"
        add_file(archive, sums, source_root + "linux/" + sums.name)
        for filename in ("kernel.config", "manifest.json"):
            add_file(archive, BUILD / "linux" / filename, source_root + "linux/" + filename)
        for path in sorted((BUILD / "linux/licenses").rglob("*")):
            if path.is_file():
                add_file(archive, path, source_root + "linux/licenses/" + path.name)
        for path in project_sources(args.output_dir):
            add_file(archive, path, source_root + "mayos-build/" + path.relative_to(ROOT).as_posix())
        add_file(archive, ROOT / "docs/third-party/dual-boot.md", source_root + "README.md")

    main_zip = args.output_dir / f"{name}.zip"
    required_outputs = ["mayos.elf", "mayos.iso", "mayos.iso.boot.json",
                        "mayos-dual.iso", "mayos-dual.iso.sha256", "mayos-dual.iso.boot.json",
                        "mayos-disk.img", "dual-boot-test-results.json", "uefi-test-results.json",
                        "gui-03-validation.json", "release03-screenshot-provenance.json",
                        "custom-regression.log", "uefi-full-test.log",
                        "custom-03-smoke.log", "custom-03-ui.log", "custom-03-easter.log",
                        "dual-boot-regression.log", "test-dual-mayos.log",
                        "test-dual-persistence.log", "test-dual-linux.log",
                        "test-uefi-custom.log", "test-uefi-cold.log",
                        "test-uefi-linux.log", "test-uefi-rescue.log",
                        "record-uefi-custom.log", "record-uefi-linux.log"]
    with zipfile.ZipFile(main_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in project_sources(args.output_dir):
            add_file(archive, path, "mayos/" + path.relative_to(ROOT).as_posix())
        for filename in required_outputs:
            add_file(archive, BUILD / filename, "mayos/build/" + filename)
        for filename in ("bzImage", "initramfs.cpio.gz", "busybox", "mayos-desktop", "kernel.config", "manifest.json"):
            add_file(archive, BUILD / "linux" / filename, "mayos/build/linux/" + filename)
        for path in sorted((BUILD / "linux/licenses").rglob("*")):
            if path.is_file():
                add_file(archive, path, "mayos/build/linux/licenses/" + path.name)
        for path in recording_manifests:
            add_file(archive, path, "mayos/build/recordings/" + path.name)

    checksums = args.output_dir / f"{name}-SHA256SUMS.txt"
    checksums.write_text("".join(f"{digest(path)}  {path.name}\n"
        for path in (main_zip, source_zip, *recording_files, BUILD / "mayos-dual.iso")))
    for path in (main_zip, source_zip, checksums):
        print(f"{path} ({path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build the independent x86_64 Linux boot payload; no elevated privileges needed.

Python 3.12+ is required for the safe tarfile extraction filter. Downloaded
archives are checked against kernel.org SHA-256 over HTTPS, not PGP-verified.
"""
from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/linux"
RELEASES_URL = "https://www.kernel.org/releases.json"
VERSION_RE = re.compile(r"[1-9][0-9]*\.[0-9]+(?:\.[0-9]+)?\Z")
INPUT_FILES = {
    "config_sha256": ROOT / "linux/kernel.config",
    "init_sha256": ROOT / "linux/init",
    "inittab_sha256": ROOT / "linux/inittab",
    "profile_sha256": ROOT / "linux/profile",
    "desktop_session_sha256": ROOT / "linux/desktop-session",
    "desktop_backend_sha256": ROOT / "linux/desktop.c",
    "desktop_gui_sha256": ROOT / "kernel/gui.c",
    "desktop_fs_sha256": ROOT / "kernel/fs.c",
    "desktop_core_header_sha256": ROOT / "kernel/core.h",
    "font_core_source_sha256": ROOT / "kernel/core.c",
    "font_generator_sha256": ROOT / "scripts/font.py",
    "photo_generator_sha256": ROOT / "scripts/photo.py",
    "photo_sha256": ROOT / "assets/easter-cat.jpg",
    "linux_readme_sha256": ROOT / "linux/README.md",
    "builder_sha256": Path(__file__).resolve(),
}
REQUIRED_CONFIG = (
    "64BIT", "BLK_DEV_INITRD", "RD_GZIP", "BINFMT_ELF", "BINFMT_SCRIPT",
    "DEVTMPFS", "PROC_FS", "SYSFS", "TMPFS", "TTY", "VT_CONSOLE",
    "SERIAL_8250_CONSOLE", "VGA_CONSOLE", "KEYBOARD_ATKBD", "ACPI",
    "EFI", "EFI_STUB", "FB", "FB_DEVICE", "FRAMEBUFFER_CONSOLE", "MOUSE_PS2",
    "INPUT_EVDEV",
)
REQUIRED_APPLETS = {
    "sh", "init", "cttyhack", "mount", "umount", "mkdir", "hostname",
    "uname", "sync", "reboot", "poweroff", "cat", "ls", "free",
}


def sha256(path: Path) -> str:
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def inputs() -> dict[str, str]:
    result = {key: sha256(path) for key, path in INPUT_FILES.items()}
    for path in sorted((ROOT / "docs/third-party").rglob("*.txt")):
        result["license:" + str(path.relative_to(ROOT))] = sha256(path)
    return result


def write_json(path: Path, data: dict) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
    tmp.replace(path)


def download(url: str, path: Path, *, offline: bool = False) -> None:
    if offline:
        if not path.is_file():
            raise RuntimeError(f"Offline cache is missing: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".part")
    print(f"Downloading {url}", flush=True)
    request = urllib.request.Request(url, headers={"User-Agent": "MayOS-Linux-builder/0.2"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, temp.open("wb") as f:
            shutil.copyfileobj(response, f, length=1024 * 1024)
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)


def selected_version(requested: str, *, offline: bool = False) -> str:
    if requested != "latest":
        if not VERSION_RE.fullmatch(requested):
            raise RuntimeError("Version must be 'latest' or a stable numeric release, e.g. 7.3.1")
        return requested
    if offline and (OUT / "manifest.json").exists():
        version = json.loads((OUT / "manifest.json").read_text())["version"]
    else:
        cached = OUT / "downloads/releases.json"
        download(RELEASES_URL, cached, offline=offline)
        data = json.loads(cached.read_text())
        version = data["latest_stable"]["version"]
        # A freshly released final major version may still be labelled
        # "mainline" until its first stable point release. latest_stable plus
        # the numeric-only version check excludes release candidates.
        if not any(r.get("version") == version and r.get("moniker") in ("stable", "mainline")
                   for r in data.get("releases", [])):
            raise RuntimeError("kernel.org latest_stable does not match a stable release entry")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise RuntimeError("Invalid stable version in release metadata")
    return version


def source_archive(version: str, *, offline: bool = False) -> tuple[Path, dict]:
    major = version.split(".")[0]
    base = f"https://cdn.kernel.org/pub/linux/kernel/v{major}.x/"
    filename = f"linux-{version}.tar.xz"
    sums = OUT / "downloads" / f"sha256sums-v{major}.asc"
    # Adopt an already downloaded official checksum list if present.
    initial = OUT / "downloads/sha256sums.asc"
    if not sums.exists() and initial.exists():
        shutil.copyfile(initial, sums)
    if not sums.exists():
        download(base + "sha256sums.asc", sums, offline=offline)
    matches = re.findall(r"^([0-9a-fA-F]{64})[ \t]+\*?" + re.escape(filename) + r"$",
                         sums.read_text(), re.MULTILINE)
    if not matches and not offline:
        download(base + "sha256sums.asc", sums)
        matches = re.findall(r"^([0-9a-fA-F]{64})[ \t]+\*?" + re.escape(filename) + r"$",
                             sums.read_text(), re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f"No unique official SHA-256 found for {filename}")
    expected = matches[0].lower()
    archive = OUT / "downloads" / filename
    if not archive.exists():
        download(base + filename, archive, offline=offline)
    if sha256(archive) != expected:
        raise RuntimeError(f"SHA-256 mismatch: {archive}; remove the bad cache and retry")
    return archive, {
        "url": base + filename, "sha256": expected,
        "archive": str(archive.relative_to(OUT)),
        "checksum_url": base + "sha256sums.asc",
        "checksum_verification": "SHA-256 against official HTTPS checksum list; PGP not verified",
    }


def extract_source(archive: Path, version: str, expected_hash: str) -> Path:
    source = OUT / "source" / f"linux-{version}"
    stamp = source / ".mayos-source-sha256"
    if stamp.exists() and stamp.read_text().strip() == expected_hash:
        return source
    if not hasattr(tarfile, "data_filter"):
        raise RuntimeError("Python 3.12+ is required for safe archive extraction")
    # Never extract across a previous partially written tree or followed symlink.
    if source.is_symlink():
        raise RuntimeError(f"Refusing symlink source directory: {source}")
    if source.exists():
        shutil.rmtree(source)
    source.parent.mkdir(parents=True, exist_ok=True)
    print(f"Extracting {archive.name}", flush=True)
    with tarfile.open(archive, "r:xz") as tar:
        for member in tar.getmembers():
            if member.name.split("/")[0] != f"linux-{version}":
                raise RuntimeError("Unexpected top-level directory in Linux archive")
        tar.extractall(source.parent, filter="data")
    stamp.write_text(expected_hash + "\n")
    return source


def validate_static_elf(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise RuntimeError(f"{path.name} must be a little-endian ELF64 binary")
    if struct.unpack_from("<H", data, 18)[0] != 62:
        raise RuntimeError(f"{path.name} must target x86_64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    if phentsize < 56 or phoff + phentsize * phnum > len(data):
        raise RuntimeError(f"{path.name} has invalid ELF program headers")
    if any(struct.unpack_from("<I", data, phoff + i * phentsize)[0] == 3 for i in range(phnum)):
        raise RuntimeError(f"{path.name} is dynamically linked; supply a static binary")


def validate_busybox(path: Path) -> tuple[list[str], str]:
    validate_static_elf(path)
    applets = subprocess.check_output([str(path), "--list"], text=True).splitlines()
    missing = REQUIRED_APPLETS.difference(applets)
    if missing:
        raise RuntimeError(f"BusyBox lacks required applets: {', '.join(sorted(missing))}")
    version = subprocess.check_output([str(path)], text=True, stderr=subprocess.STDOUT).splitlines()[0]
    return applets, version


def busybox_path(requested: str | None) -> Path:
    cached = OUT / "busybox"
    candidate = requested or os.environ.get("BUSYBOX")
    if candidate:
        found = Path(candidate).expanduser().resolve()
    elif cached.exists():
        found = cached
    else:
        executable = shutil.which("busybox")
        if not executable:
            raise RuntimeError("Install busybox-static or pass --busybox /path/to/static/busybox")
        found = Path(executable).resolve()
    validate_busybox(found)
    if found != cached:
        shutil.copyfile(found, cached)
        cached.chmod(0o755)
    return cached


def cpio_member(stream: io.BytesIO, inode: int, name: str, mode: int,
                data: bytes = b"", *, major: int = 0, minor: int = 0) -> None:
    encoded = name.encode() + b"\0"
    fields = (inode, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0,
              len(data), 0, 0, major, minor, len(encoded), 0)
    stream.write(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
    stream.write(encoded)
    stream.write(b"\0" * (-stream.tell() % 4))
    stream.write(data)
    stream.write(b"\0" * (-stream.tell() % 4))


def build_initramfs(busybox: Path, version: str) -> dict:
    applets, busybox_version = validate_busybox(busybox)
    entries: list[tuple[str, int, bytes, int, int]] = []
    for directory in ("bin", "sbin", "usr", "usr/bin", "usr/sbin", "usr/share", "usr/share/doc",
                      "usr/share/doc/mayos", "usr/share/licenses", "etc", "dev",
                      "proc", "sys", "tmp", "run", "root", "mnt"):
        entries.append((directory, stat.S_IFDIR | (0o1777 if directory == "tmp" else 0o755), b"", 0, 0))
    entries.append(("bin/busybox", stat.S_IFREG | 0o755, busybox.read_bytes(), 0, 0))
    entries.append(("bin/mayos-desktop", stat.S_IFREG | 0o755, (OUT / "mayos-desktop").read_bytes(), 0, 0))
    for applet in sorted(set(applets)):
        if re.fullmatch(r"[A-Za-z0-9_.\[\]-]+", applet) and applet != "busybox":
            entries.append(("bin/" + applet, stat.S_IFLNK | 0o777, b"busybox", 0, 0))
    for target, source, mode in (("init", "init", 0o755), ("etc/inittab", "inittab", 0o644),
                                 ("etc/profile", "profile", 0o644),
                                 ("bin/mayos-session", "desktop-session", 0o755),
                                 ("usr/share/doc/mayos/linux-readme.txt", "README.md", 0o644)):
        entries.append((target, stat.S_IFREG | mode, (ROOT / "linux" / source).read_bytes(), 0, 0))
    text_files = {
        "etc/os-release": 'NAME="MayOS Linux Live"\nID=mayos-linux\nVERSION="0.3.0-pre1"\nPRETTY_NAME="MayOS Linux Live 0.3.0-pre1"\n',
        "etc/passwd": "root:x:0:0:root:/root:/bin/sh\n",
        "etc/group": "root:x:0:\n",
        "etc/hostname": "MayOS-Linux\n",
        "etc/mayos-kernel-version": version + "\n",
    }
    for name, content in text_files.items():
        entries.append((name, stat.S_IFREG | 0o644, content.encode(), 0, 0))
    license_files = list((OUT / "licenses").glob("*"))
    license_files.extend((ROOT / "docs/third-party/dual-boot-licenses").glob("*.txt"))
    license_files.extend((ROOT / "docs/third-party").glob("*-copyright.txt"))
    for path in sorted(license_files):
        if path.is_file():
            entries.append(("usr/share/licenses/" + path.name, stat.S_IFREG | 0o644, path.read_bytes(), 0, 0))
    for name, major, minor in (("console", 5, 1), ("null", 1, 3), ("tty1", 4, 1), ("ttyS0", 4, 64)):
        entries.append(("dev/" + name, stat.S_IFCHR | 0o600, b"", major, minor))
    stream = io.BytesIO()
    for inode, (name, mode, data, major, minor) in enumerate(entries, 1):
        cpio_member(stream, inode, name, mode, data, major=major, minor=minor)
    cpio_member(stream, len(entries) + 1, "TRAILER!!!", 0)
    payload = gzip.compress(stream.getvalue(), compresslevel=9, mtime=0)
    (OUT / "initramfs.cpio.gz").write_bytes(payload)
    return {"version": busybox_version, "sha256": sha256(busybox),
            "path": "busybox", "license": "GPL-2.0-only",
            "corresponding_source_required": True}


def verify_manifest(*, check_inputs: bool = True) -> tuple[dict | None, list[str]]:
    path = OUT / "manifest.json"
    if not path.exists():
        return None, ["No Linux payload manifest; run make linux"]
    try:
        manifest = json.loads(path.read_text())
        errors = []
        if manifest.get("schema_version") != 1 or manifest.get("architecture") != "x86_64":
            errors.append("Unsupported manifest schema or architecture")
        if not VERSION_RE.fullmatch(manifest.get("version", "")):
            errors.append("Invalid kernel version")
        for name in ("bzImage", "initramfs", "config", "desktop"):
            artifact = manifest["artifacts"][name]
            file = (OUT / artifact["path"]).resolve()
            if not file.is_relative_to(OUT.resolve()) or not file.is_file():
                errors.append(f"Missing or unsafe artifact: {name}")
            elif sha256(file) != artifact["sha256"]:
                errors.append(f"SHA-256 mismatch: {name}")
        if check_inputs and manifest.get("inputs") != inputs():
            errors.append("Linux build inputs have changed")
        return manifest, errors
    except (ValueError, KeyError, TypeError, OSError) as error:
        return None, [f"Invalid Linux manifest: {error}"]


def build(args: argparse.Namespace) -> dict:
    OUT.mkdir(parents=True, exist_ok=True)
    version = selected_version(args.version, offline=args.offline)
    busybox = busybox_path(args.busybox)
    manifest, errors = verify_manifest()
    if (not args.rebuild and not errors and manifest and manifest["version"] == version
            and manifest["busybox"]["sha256"] == sha256(busybox)):
        print(f"Reusing verified Linux {version} payload", flush=True)
        return manifest
    build_inputs = inputs()
    for tool in ("gcc", "make", "ld", "flex", "bison", "bc", "rsync"):
        if not shutil.which(tool):
            raise RuntimeError(f"Missing build dependency: {tool}")
    desktop_compiler = args.desktop_cc or os.environ.get("MAYOS_LINUX_CC") or os.environ.get("MAYOS_MUSL_CC") or "musl-gcc"
    desktop_compiler = shutil.which(desktop_compiler)
    if not desktop_compiler:
        raise RuntimeError("Install musl-tools or set MAYOS_LINUX_CC to a musl-gcc compiler")
    archive, source_info = source_archive(version, offline=args.offline)
    source = extract_source(archive, version, source_info["sha256"])
    output = OUT / "objects" / version
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update({"KBUILD_BUILD_USER": "mayos", "KBUILD_BUILD_HOST": "builder",
                "KBUILD_BUILD_VERSION": "1", "KBUILD_BUILD_TIMESTAMP": "1970-01-01 00:00:00 +0000",
                "KCONFIG_NOTIMESTAMP": "1", "LOCALVERSION": ""})
    make = ["make", "-C", str(source), f"O={output}", "ARCH=x86_64"]
    log = OUT / "build.log"
    with log.open("w") as log_stream:
        def run(arguments: list[str]) -> None:
            print("Running: " + " ".join(arguments), flush=True)
            result = subprocess.run(arguments, cwd=ROOT, env=env, stdout=log_stream, stderr=subprocess.STDOUT)
            log_stream.flush()
            if result.returncode:
                tail = log.read_text(errors="replace").splitlines()[-45:]
                raise RuntimeError("Build failed. Last log lines:\n" + "\n".join(tail))
        run(make + [f"KCONFIG_ALLCONFIG={ROOT / 'linux/kernel.config'}", "allnoconfig"])
        config = (output / ".config").read_text()
        missing = [name for name in REQUIRED_CONFIG if f"CONFIG_{name}=y\n" not in config]
        if missing:
            raise RuntimeError("Kconfig did not enable required boot options: " + ", ".join(missing))
        run(make + [f"-j{args.jobs}", "bzImage"])
        run(make + [f"INSTALL_HDR_PATH={OUT / 'headers'}", "headers_install"])
        run([sys.executable, str(ROOT / "scripts/font.py")])
        run([sys.executable, str(ROOT / "scripts/photo.py")])
        run([desktop_compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-DMAYOS_LINUX", "-fno-pie", "-no-pie", "-static",
             "-I" + str(ROOT / "kernel"), "-I" + str(ROOT / "build"),
             "-isystem", str(OUT / "headers/include"),
             str(ROOT / "linux/desktop.c"), str(ROOT / "kernel/fs.c"),
             str(ROOT / "kernel/gui.c"), "-o", str(OUT / "mayos-desktop")])
        validate_static_elf(OUT / "mayos-desktop")
    kernel = output / "arch/x86/boot/bzImage"
    release = (output / "include/config/kernel.release").read_text().strip()
    if release != version:
        raise RuntimeError(f"Built kernel release {release!r} does not match requested version {version!r}")
    shutil.copyfile(kernel, OUT / "bzImage")
    shutil.copyfile(output / ".config", OUT / "kernel.config")
    licenses = OUT / "licenses"
    licenses.mkdir(exist_ok=True)
    for source_name, target in (("COPYING", "Linux-COPYING"),
                                ("LICENSES/preferred/GPL-2.0", "Linux-GPL-2.0"),
                                ("LICENSES/exceptions/Linux-syscall-note", "Linux-syscall-note")):
        shutil.copyfile(source / source_name, licenses / target)
    busybox_info = build_initramfs(busybox, version)
    artifacts = {name: {"path": filename, "sha256": sha256(OUT / filename)}
                 for name, filename in (("bzImage", "bzImage"), ("initramfs", "initramfs.cpio.gz"),
                                        ("config", "kernel.config"), ("desktop", "mayos-desktop"))}
    if inputs() != build_inputs:
        raise RuntimeError("Build inputs changed during compilation; rerun to produce a coherent payload")
    manifest = {"schema_version": 1, "version": version, "kernel_release": release,
                "architecture": "x86_64", "source": source_info, "artifacts": artifacts,
                "busybox": busybox_info, "inputs": build_inputs,
                "desktop": {"process": "mayos-desktop", "linkage": "static musl", "display": "Linux fbdev",
                            "input": "Linux evdev", "filesystem": "process RAM; no block-device writes",
                            "compiler": subprocess.check_output([desktop_compiler, "--version"], text=True).splitlines()[0]},
                "kernel_build_timestamp": "1970-01-01 00:00:00 +0000 (fixed for reproducibility)",
                "built_at": dt.datetime.now(dt.timezone.utc).isoformat()}
    write_json(OUT / "manifest.json", manifest)
    print(f"Built Linux {release}; manifest: {OUT / 'manifest.json'}", flush=True)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    command = sub.add_parser("build", help="Fetch verified source and build Linux + initramfs")
    command.add_argument("--version", default="latest")
    command.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    command.add_argument("--busybox", help="Path to a static x86_64 BusyBox")
    command.add_argument("--desktop-cc", help="Path to a static musl userspace compiler (default: musl-gcc)")
    command.add_argument("--offline", action="store_true")
    command.add_argument("--rebuild", action="store_true")
    command = sub.add_parser("resolve", help="Print the selected stable version")
    command.add_argument("--version", default="latest")
    command.add_argument("--offline", action="store_true")
    sub.add_parser("status", help="Check payload hashes and current build inputs")
    args = parser.parse_args()
    try:
        if args.command == "build":
            if args.jobs < 1:
                parser.error("--jobs must be positive")
            build(args)
        elif args.command == "resolve":
            print(selected_version(args.version, offline=args.offline))
        else:
            manifest, errors = verify_manifest()
            print(json.dumps({"valid": not errors, "errors": errors, "manifest": manifest}, indent=2))
            return 1 if errors else 0
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Linux build error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

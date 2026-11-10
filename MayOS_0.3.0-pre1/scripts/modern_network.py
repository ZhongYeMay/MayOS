"""Build the native libcurl worker and package its actual runtime dependencies.

The desktop remains static musl; the worker has a separate distro-libc ABI.
Only locally built/trusted ELF files are passed to ldd. No third-party binary
is downloaded here. Redistributors must provide corresponding library sources.
"""
import hashlib
import json
from pathlib import Path
import re
import shlex
import stat
import subprocess


def runtime(root: Path, output: Path):
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "mayos-fetch"
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "libcurl"], text=True))
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(root / "linux/fetch.c"), *flags, "-o", str(binary)], check=True)
    capabilities = subprocess.check_output([str(binary), "--capabilities"], text=True).strip()
    linked = subprocess.check_output(["ldd", str(binary)], text=True)
    if "not found" in linked:
        raise RuntimeError("Missing native network runtime dependency")
    files = {"usr/bin/mayos-fetch": binary,
             "etc/ssl/certs/ca-certificates.crt": Path("/etc/ssl/certs/ca-certificates.crt")}
    for path in re.findall(r"(?<!\S)(/[^\s]+)", linked):
        files[path.lstrip("/")] = Path(path)
    # Include Debian/Ubuntu packaging copyright texts for all linked libraries.
    packages = set()
    for file in list(files.values()):
        if file == binary:
            continue
        owner = subprocess.run(["dpkg-query", "-S", str(file)], text=True, capture_output=True)
        if owner.returncode:
            owner = subprocess.run(["dpkg-query", "-S", str(file.resolve())], text=True, capture_output=True)
        for line in owner.stdout.splitlines():
            if ": /" not in line:
                continue
            package = line.split(": /", 1)[0].split(":", 1)[0]
            copyright_file = Path("/usr/share/doc") / package / "copyright"
            if copyright_file.is_file():
                files[f"usr/share/licenses/network-{package}.txt"] = copyright_file
                packages.add(package)
    package_versions = {}
    for package in sorted(packages):
        package_versions[package] = subprocess.check_output(
            ["dpkg-query", "-W", "-f", "${Version}; source=${source:Package} ${source:Version}", package], text=True)
    manifest = {"capabilities": capabilities, "packages": package_versions, "files": {
        name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in sorted(files.items())}}
    metadata = output / "network-runtime.json"
    metadata.write_text(json.dumps(manifest, indent=2) + "\n")
    files["usr/share/doc/mayos/network-runtime.json"] = metadata
    entries = []
    directories = set()
    for name in files:
        for parent in Path(name).parents:
            if str(parent) != ".": directories.add(str(parent))
    for name in sorted(directories, key=lambda d: (d.count("/"), d)):
        entries.append((name, stat.S_IFDIR | 0o755, b"", 0, 0))
    for name, path in sorted(files.items()):
        mode = 0o755 if name == "usr/bin/mayos-fetch" or ".so" in name else 0o644
        entries.append((name, stat.S_IFREG | mode, path.read_bytes(), 0, 0))
    return entries

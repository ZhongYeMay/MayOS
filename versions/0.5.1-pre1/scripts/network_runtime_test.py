#!/usr/bin/env python3
"""Validate the worker and all its packaged runtime libraries and CA bundle."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from modern_network import runtime

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
    stage = Path(folder)
    entries = runtime(root, root / "build/network-runtime-test")
    for name, mode, data, _, _ in entries:
        target = stage / name
        if mode & 0o170000 == 0o040000:
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(mode & 0o777)
    assert (stage / "etc/ssl/certs/ca-certificates.crt").stat().st_size > 1000
    assert (stage / "usr/share/doc/mayos/network-runtime.json").is_file()
    if "--chroot" in sys.argv:
        subprocess.run(["sudo", "-n", "chroot", str(stage), "/usr/bin/mayos-fetch", "--capabilities"], check=True)
    else:
        loaders = list(stage.glob("lib*/**/ld-linux*.so*"))
        assert loaders, "ELF interpreter not packaged"
        libraries = sorted({str(path.parent) for path in stage.rglob("*.so*")})
        subprocess.run([str(loaders[0]), "--library-path", ":".join(libraries),
                        str(stage / "usr/bin/mayos-fetch"), "--capabilities"], check=True)
print("PASS: packaged native network worker, ELF runtime, licenses manifest and CA bundle")

#!/usr/bin/env python3
"""Host-level updater tests; run before native QEMU integration."""
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BUILD.mkdir(exist_ok=True)
subprocess.run(["python3", str(ROOT / "scripts/make_update_catalog.py")], check=True)
subprocess.run([
    "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I" + str(ROOT / "kernel"), "-I" + str(BUILD),
    str(ROOT / "kernel/updater.c"), str(ROOT / "kernel/update_sha256.c"),
    str(ROOT / "scripts/updater_test.c"),
    "-o", str(BUILD / "updater-test"),
], check=True)
subprocess.run([str(BUILD / "updater-test")], check=True)
subprocess.run([
    "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I" + str(ROOT / "kernel"),
    str(ROOT / "kernel/update_bootplan.c"),
    str(ROOT / "scripts/updater_boot_test.c"),
    "-o", str(BUILD / "updater-boot-test"),
], check=True)
subprocess.run([str(BUILD / "updater-boot-test")], check=True)

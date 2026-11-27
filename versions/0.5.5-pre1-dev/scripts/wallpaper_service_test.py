#!/usr/bin/env python3
"""Host-only one-cycle integration tests for the actual BusyBox wallpaper service.

Isolates every persistent path under a temporary directory; no mounted disks,
network, real Bing images or host /run are ever touched.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "linux/wallpaper-service").read_text()
SIZE = 1280 * 720 * 2


def run(service, *, mode, image, base):
    env = os.environ.copy()
    env.pop("MAYOS_PROFILE_DIR", None)
    env.update(MAYOS_TEST_WORKER_MODE=mode, MAYOS_TEST_IMAGE=str(image))
    subprocess.run(["sh", str(service)], check=True, env=env, timeout=15,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)


with tempfile.TemporaryDirectory(prefix="mayos-wallpaper-service-") as folder:
    root = Path(folder)
    run_dir = root / "run/mayos-wallpaper"
    mount = root / "mnt/mayos-profile"
    cache = mount / "bing-daily"
    env_file = root / "run/mayos-profile-env"
    worker = root / "fake-wallpaper-worker"
    service = root / "wallpaper-service"
    candidate = SOURCE
    for old, new in (
        ("/run/mayos-wallpaper", str(run_dir)),
        ("/run/mayos-profile-env", str(env_file)),
        ("/mnt/mayos-profile", str(mount)),
        ("/usr/bin/mayos-wallpaper", str(worker)),
        ('sleep "$delay"', "break"),
    ):
        assert old in candidate, f"Service fixture contract changed: {old}"
        candidate = candidate.replace(old, new)
    service.write_text(candidate)
    subprocess.run(["sh", "-n", str(service)], check=True)
    worker.write_text(
        '#!/bin/sh\n'
        'if [ "$MAYOS_TEST_WORKER_MODE" = fail ]; then exit 1; fi\n'
        'cp "$MAYOS_TEST_IMAGE" "$1"\n'
        "printf '20261128\\n' > \"$2\"\n"
    )
    worker.chmod(0o755)
    current = root / "image.r565"
    current.write_bytes(b"B" * SIZE)

    # No profile env or opt-in marker: write only volatile /run.
    run(service, mode="update", image=current, base=root)
    assert (run_dir / "current.r565").read_bytes() == b"B" * SIZE
    assert not cache.exists(), "RAM-only session wrote a profile directory"

    # Spoofed profile env still must not enable persistence without marker.
    mount.mkdir(parents=True)
    env_file.write_text(f"MAYOS_PROFILE_DIR={mount}\nexport MAYOS_PROFILE_DIR\n")
    (mount / ".mayos-profile-enabled").write_text("WRONG-MARKER\n")
    run(service, mode="update", image=current, base=root)
    assert not cache.exists(), "Invalid profile marker enabled persistence"

    # Marked opt-in ext4 equivalent: restore valid yesterday image offline.
    (mount / ".mayos-profile-enabled").write_text("MayOS-Profile-v1\n")
    cache.mkdir()
    (cache / "current.r565").write_bytes(b"A" * SIZE)
    (cache / "current.date").write_text("20261127\n")
    run(service, mode="fail", image=current, base=root)
    assert (run_dir / "current.r565").read_bytes() == b"A" * SIZE
    assert (run_dir / "current.date").read_text() == "20261127\n"
    assert (cache / "current.r565").read_bytes() == b"A" * SIZE

    # Next successful daily picture is staged into the marked profile.
    run(service, mode="update", image=current, base=root)
    assert (cache / "current.r565").read_bytes() == b"B" * SIZE
    assert (cache / "current.date").read_text() == "20261128\n"
    before = ((cache / "current.r565").stat().st_mtime_ns,
              (cache / "current.date").stat().st_mtime_ns)
    time.sleep(.05)
    run(service, mode="update", image=current, base=root)
    after = ((cache / "current.r565").stat().st_mtime_ns,
             (cache / "current.date").stat().st_mtime_ns)
    assert before == after, "Unchanged Bing date rewrote ext4 cache"

    # The cache directory itself must not be followed if symlinked.
    shutil.rmtree(cache)
    outside = root / "outside-allowed-profile"
    outside.mkdir()
    cache.symlink_to(outside, target_is_directory=True)
    run(service, mode="update", image=current, base=root)
    assert list(outside.iterdir()) == [], "Symlink cache escaped allowed profile"

print("PASS: wallpaper service RAM-only, opt-in marker, offline restore, "
      "daily update, no repeat write, symlink-directory refusal")

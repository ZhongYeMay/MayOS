#!/usr/bin/env python3
"""A/B upgrade of a MayKernel Live ISO with a separate QEMU MayFS disk."""

import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time


SLOTS = ("A", "B")
VERSION = re.compile(r"\d+\.\d+\.\d+(?:-pre\d+)?")
DIGEST = re.compile(r"[0-9a-f]{64}")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def durable_replace(source, target):
    os.replace(source, target)
    descriptor = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def set_state(directory, state):
    with tempfile.NamedTemporaryFile(dir=directory, delete=False, mode="w") as output:
        temporary = Path(output.name)
        json.dump(state, output)
        output.flush()
        os.fsync(output.fileno())
    try:
        durable_replace(temporary, directory / "active.json")
    finally:
        temporary.unlink(missing_ok=True)


def slot_image(directory, record):
    if not isinstance(record, dict) or record.get("slot") not in SLOTS:
        raise ValueError("Invalid stored boot slot")
    image = directory / (record["slot"] + ".iso")
    expected = record.get("sha256", "")
    if not DIGEST.fullmatch(expected) or digest(image) != expected:
        raise ValueError(f"Slot {record['slot']} fails SHA-256 verification")
    return image


@contextmanager
def stage(directory, source, slot, version, expected):
    if not VERSION.fullmatch(version) or not DIGEST.fullmatch(expected):
        raise ValueError("Specify a MayOS version and a trusted SHA-256 digest")
    record = {"slot": slot, "version": version, "sha256": expected}
    with tempfile.NamedTemporaryFile(dir=directory, delete=False) as output:
        temporary = Path(output.name)
        try:
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, output, length=1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
            if digest(temporary) != expected:
                raise ValueError("ISO SHA-256 does not match")
            with temporary.open("rb") as stream:
                stream.seek(32769)
                if stream.read(5) != b"CD001":
                    raise ValueError("Candidate is not an ISO 9660 image")
            yield record, temporary
        finally:
            temporary.unlink(missing_ok=True)


def qemu_args(qemu, image, disk):
    return [qemu, "-machine", "pc", "-accel", "tcg", "-cpu", "max",
            "-m", "256", "-vga", "std", "-nic", "none", "-cdrom", str(image),
            "-drive", f"file={str(disk).replace(',', ',,')},format=raw,if=ide,index=0",
            "-boot", "d", "-no-reboot"]


def trial(qemu, image, disk, version, timeout, log):
    # QEMU keeps guest writes in a temporary overlay. Even a failing guest
    # cannot change the original MayFS disk through its virtual ATA drive.
    args = qemu_args(qemu, image, disk)
    args += ["-snapshot", "-display", "none", "-monitor", "none",
             "-serial", f"file:{log}"]
    markers = {"MayOS: MayFS mounted", "MayOS: desktop ready",
               f"MayOS Workstation {version} / MayShell"}
    with subprocess.Popen(args) as process:
        try:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    break
                if log.is_file() and markers <= set(log.read_text(errors="replace").splitlines()):
                    return
                time.sleep(.1)
            raise ValueError(f"Trial boot failed. The current ISO remains selected. See {log}")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def regular(path):
    path = path.resolve(strict=True)
    if not path.is_file():
        raise ValueError(f"Expected a regular file: {path}")
    return path


def operate(args):
    directory = args.directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    state_path = directory / "active.json"
    with (directory / ".lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.action == "init":
            if state_path.exists():
                raise ValueError("Boot slots are already initialized")
            disk = regular(args.disk)
            source = regular(args.iso)
            if disk.is_relative_to(directory) or source.samefile(disk):
                raise ValueError("Keep the MayFS disk and boot slots in separate files")
            with stage(directory, source, "A", args.version, args.sha256) as (record, image):
                durable_replace(image, directory / "A.iso")
                set_state(directory, {"active": record, "previous": None, "disk": str(disk)})
            return 0

        state = json.loads(state_path.read_text())
        active = state["active"]
        active_image = slot_image(directory, active)
        if args.action == "status":
            print(json.dumps(state, indent=2))
            return 0
        if args.action == "rollback":
            previous = state.get("previous")
            if not previous:
                raise ValueError("No previous verified boot slot")
            slot_image(directory, previous)
            set_state(directory, {**state, "active": previous, "previous": active})
            return 0

        disk = regular(Path(state["disk"]))
        if disk.is_relative_to(directory) or disk.samefile(active_image):
            raise ValueError("Use a separate MayFS disk outside the boot-slot directory")
        if args.action == "run":
            return subprocess.call(qemu_args(args.qemu, active_image, disk))
        source = regular(args.iso)
        if source.samefile(disk):
            raise ValueError("The candidate ISO and MayFS disk must be different files")
        if args.version == active["version"]:
            raise ValueError("The requested version is already selected")
        inactive = "B" if active["slot"] == "A" else "A"
        with stage(directory, source, inactive, args.version, args.sha256) as (candidate, image):
            print(f"Starting isolated trial of MayOS {args.version}.", flush=True)
            with tempfile.TemporaryDirectory(prefix="mayos-trial-") as session:
                trial_log = Path(session) / "serial.log"
                try:
                    trial(args.qemu, image, disk, args.version, args.timeout, trial_log)
                finally:
                    if trial_log.exists():
                        shutil.copyfile(trial_log, directory / "last-trial.log")
            if digest(image) != candidate["sha256"]:
                raise ValueError("Candidate changed after verification")
            # The old active ISO stays valid if interrupted between these writes.
            if state.get("previous"):
                set_state(directory, {**state, "previous": None})
            durable_replace(image, directory / (inactive + ".iso"))
            set_state(directory, {**state, "active": candidate, "previous": active})
        print(f"MayOS {args.version} selected. Previous ISO available through rollback.")
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    commands = parser.add_subparsers(dest="action", required=True)
    for name in ("init", "upgrade", "run", "status", "rollback"):
        command = commands.add_parser(name)
        if name in ("init", "upgrade"):
            command.add_argument("--iso", type=Path, required=True)
            command.add_argument("--sha256", required=True)
            command.add_argument("--version", required=True)
        if name == "init":
            command.add_argument("--disk", type=Path, required=True)
        if name in ("upgrade", "run"):
            command.add_argument("--qemu", default="qemu-system-x86_64")
        if name == "upgrade":
            command.add_argument("--timeout", type=int, choices=range(5, 181),
                                 default=60, metavar="5..180")
    args = parser.parse_args()
    try:
        return operate(args)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())

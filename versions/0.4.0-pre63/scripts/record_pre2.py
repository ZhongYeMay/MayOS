#!/usr/bin/env python3
"""Record real MayOS 0.4.0-pre2 UEFI boot and native GUI interaction.

The ISO must be a published, SHA256-verified pre2 Release asset. This script
takes QMP framebuffer samples from a real QEMU guest; it NEVER draws frames
or overlays promotional imagery into the recording.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import time

from record_boot import Session, sha


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, choices=("custom", "linux"))
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ovmf-code", type=Path, required=True)
    parser.add_argument("--ovmf-vars", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--capture-commit", required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--fps", type=int, default=10)
    args = parser.parse_args()
    args.firmware = "uefi"
    args.iso = args.iso.resolve()
    args.output = args.output.resolve()
    args.ovmf_code = args.ovmf_code.resolve()
    args.ovmf_vars = args.ovmf_vars.resolve()
    args.release = "0.4.0-pre2"

    if not re.fullmatch(r"[0-9a-f]{40}", args.source_commit):
        parser.error("--source-commit must be an exact commit hash")
    if not re.fullmatch(r"[0-9a-f]{40}", args.capture_commit):
        parser.error("--capture-commit must be an exact commit hash")
    if not 6 <= args.fps <= 20:
        parser.error("--fps must be between 6 and 20")
    if not args.iso.is_file():
        parser.error("--iso is missing; do not attempt a simulated recording")

    iso_hash = sha(args.iso)
    start = datetime.now(timezone.utc).isoformat()
    session = Session(args)
    actions = ["UEFI_firmware", "GRUB_menu", "kernel_selection"]
    try:
        session.start_recording()
        session.wait("custom kernel", timeout=90)
        session.key("down" if args.kernel == "linux" else "up")
        session.hold(2)
        session.key("ret")
        session.wait("MayOS: desktop ready", timeout=180)
        session.hold(3)

        # Live input events against the tested 0.4.0-pre2 native desktop.
        session.click(948, 94)  # close default settings window
        session.hold(2)
        actions.append("real_Material_desktop")

        session.click(730, 20)  # clock panel (icon-only status)
        session.hold(2)
        session.click(610, 430)
        actions.append("clock_popover")

        session.click(789, 20)  # network/quick settings
        session.hold(2)
        session.click(615, 430)
        actions.append("network_quick_settings")

        session.click(248, 717)  # app grid in centered Dock
        session.hold(3)
        session.key("esc")
        session.click(346, 717)  # Files via Dock
        session.hold(2)
        actions.extend(["app_launcher", "file_manager"])

        session.key("f1")  # universal MayShell shortcut in both kernels
        session.hold(2)
        offset = len(session.text())
        session.command("neofetch")
        session.wait("Kernel:", timeout=45, offset=offset)
        session.hold(3)
        actions.append("neofetch_kernel_information")

        session.command("net")
        session.hold(3)
        actions.append("network_status")
        session.finish_recording()
    finally:
        session.close()

    if sha(args.iso) != iso_hash:
        raise RuntimeError("ISO changed during recording; discard output")
    if session.frames < args.fps * 18:
        raise RuntimeError("Insufficient live QEMU frames were recorded")
    result = {
        "mayos_release": args.release,
        "kernel": args.kernel,
        "firmware": "uefi",
        "source_commit": args.source_commit,
        "capture_script_commit": args.capture_commit,
        "iso": args.iso.name,
        "iso_sha256": iso_hash,
        "video": args.output.name,
        "video_sha256": sha(args.output),
        "fps": args.fps,
        "frames": session.frames,
        "duration_seconds": session.frames / args.fps,
        "started_at_utc": start,
        "capture": "Real-time QEMU framebuffer via QMP; no synthesized UI frames",
        "actions": actions,
        "qemu": subprocess.check_output([args.qemu, "--version"], text=True).splitlines()[0],
        "ovmf_code_sha256": sha(args.ovmf_code),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()

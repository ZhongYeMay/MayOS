#!/usr/bin/env python3
"""Record actual MayOS Dock + titlebar behavior inside a fresh QEMU guest.

PS/2 mouse and keyboard input is sent to the native 64-bit MayKernel. The MP4
uses unmodified screendump frames from the running VM; PNG stills are captured
separately from that same guest. Assertions check the guest serial trace.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess

from PIL import Image
from record_boot import Session, sha


def snapshot(vm, directory: Path, name: str) -> Path:
    ppm = directory / (name + ".ppm")
    png = directory / (name + ".png")
    vm.q("screendump", {"filename": str(ppm)})
    with Image.open(ppm) as picture:
        if picture.width < 640 or picture.height < 480:
            raise AssertionError("QEMU framebuffer is unexpectedly small")
        picture.convert("RGB").save(png)
    ppm.unlink()
    return png


def checked_click(vm, x, y, expected):
    offset = len(vm.text())
    vm.click(x, y)
    vm.wait(expected, timeout=15, offset=offset)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, required=True)
    ap.add_argument("--output-dir", type=Path, required=True)
    ap.add_argument("--source-commit", required=True)
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--fps", type=int, default=10)
    args = ap.parse_args()
    assert re.fullmatch(r"[a-f0-9]{40}", args.source_commit)
    args.iso = args.iso.resolve()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    assert args.iso.is_file()
    args.kernel = "custom"
    args.firmware = "bios"
    args.ovmf_code = args.ovmf_vars = None
    args.output = args.output_dir / "MayOS-Desktop-Dock-Titlebar-QEMU.mp4"
    start = datetime.now(timezone.utc).isoformat()
    original_iso_sha = sha(args.iso)
    actions = []
    vm = Session(args)
    try:
        vm.start_recording()
        vm.hold(1.4)
        vm.key("home")
        vm.key("ret")
        vm.wait("MayOS: CPU long mode active (64-bit)", timeout=65)
        vm.wait("MayOS: desktop ready", timeout=40)
        vm.hold(1.4)
        snapshot(vm, args.output_dir, "MayOS-desktop-initial")
        actions.append("real native BIOS boot + MayOS desktop")

        vm.key("f1")
        vm.command("calc")
        vm.hold(1.2)
        snapshot(vm, args.output_dir, "MayOS-dock-calculator-running")
        actions.append("open Calculator, automatically append Dock icon")

        # Pinned: Files/Settings/Terminal; transient: Calculator; Trash.
        # All coordinates use the same 57-pixel grid as kernel/gui.c.
        first_running = (1024 - (5 * 57 + 16)) // 2 + 18 + 3 * 57 + 20
        dock_y = 768 - 92 + 35
        checked_click(vm, first_running, dock_y, "MayOS: Dock minimized Calculator")
        vm.hold(1.1)
        snapshot(vm, args.output_dir, "MayOS-dock-calculator-minimized")
        checked_click(vm, first_running, dock_y, "MayOS: Dock focused Calculator")
        vm.hold(1.1)
        actions.append("running indicator persists across minimize/restore")

        vm.key("f1")
        offset = len(vm.text())
        vm.command("minesweeper easy")
        vm.wait("MayOS Minesweeper ready", offset=offset, timeout=20)
        vm.hold(1.5)
        snapshot(vm, args.output_dir, "MayOS-dock-two-running-apps")
        actions.append("second temporary app enters Dock in launch order")

        two_left = (1024 - (6 * 57 + 16)) // 2
        calc_center = two_left + 18 + 3 * 57 + 20
        mines_center = two_left + 18 + 4 * 57 + 20
        checked_click(vm, calc_center, dock_y, "MayOS: Dock focused Calculator")
        vm.hold(1.1)
        snapshot(vm, args.output_dir, "MayOS-dock-calculator-front")
        checked_click(vm, mines_center, dock_y, "MayOS: Dock focused Minesweeper")
        vm.hold(1.1)
        snapshot(vm, args.output_dir, "MayOS-dock-minesweeper-front")
        actions.append("activate background windows using their Dock icons")

        # Close button is now unified with the actual rendered title controls.
        vm.click(122 + 780 - 24, 88 + 22)
        vm.hold(1.2)
        snapshot(vm, args.output_dir, "MayOS-dock-after-minesweeper-close")
        vm.click(287 + 450 - 24, 125 + 22)
        vm.hold(1.2)
        snapshot(vm, args.output_dir, "MayOS-dock-after-all-close")
        actions.append("titlebar close -> remove the corresponding transient Dock icon")
        vm.finish_recording()
    finally:
        vm.close()

    assert sha(args.iso) == original_iso_sha, "ISO changed during recording"
    assert args.output.stat().st_size > 30_000
    assert vm.frames >= 20 * args.fps
    stream = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height", "-of", "json",
        str(args.output)], text=True))["streams"][0]
    assert stream["codec_name"] == "h264" and stream["width"] == 1024
    assert stream["height"] == 768
    report = {
        "name": "MayOS desktop Dock and titlebar native QEMU video",
        "source_commit": args.source_commit,
        "ISO_sha256": original_iso_sha,
        "recorded_utc": start,
        "video": args.output.name,
        "video_sha256": sha(args.output),
        "fps": args.fps,
        "frames": vm.frames,
        "duration_seconds": vm.frames / args.fps,
        "capture": "Real QEMU framebuffer captured via QMP; no reconstructed frames",
        "actions": actions,
    }
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()

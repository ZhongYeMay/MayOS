#!/usr/bin/env python3
"""Capture four *real* MayDOS GUI workflows from QEMU's running framebuffer.

Each independently recorded clip starts a new BIOS+MayKernel QEMU session and
uses actual PS/2 input. Neither screenshots nor synthetic window mockups are
inserted into the MP4. Fails closed if GUI actions do not produce guest proof.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

from record_boot import Session, sha

SCENES = ("calculator", "minesweeper", "maynote", "extensions")


def command(vm, value, confirmation):
    # HMP sendkey needs symbolic names for shifted arithmetic punctuation.
    names = {' ': 'spc', '/': 'slash', '.': 'dot',
             '-': 'minus', '_': 'shift-minus', ':': 'shift-semicolon',
             '+': 'shift-equal', '*': 'shift-8',
             '(': 'shift-9', ')': 'shift-0'}
    previous = len(vm.text())
    for ch in value:
        vm.key(names.get(ch, ('shift-' + ch.lower()) if ch.isupper() else ch))
    vm.key('ret')
    vm.wait(confirmation, timeout=30, offset=previous)


def move_mouse(vm, x, y):
    """Move the emulated PS/2 mouse without clicking intermediate cells."""
    while (vm.mx, vm.my) != (x, y):
        dx = max(-50, min(50, x - vm.mx))
        dy = max(-50, min(50, y - vm.my))
        events = []
        if dx:
            events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
        if dy:
            events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
        vm.q("input-send-event", {"events": events})
        vm.mx += dx
        vm.my += dy
        vm.hold(0.05)


def right_click(vm, x, y):
    move_mouse(vm, x, y)
    for down in (True, False):
        vm.q("input-send-event",
             {"events": [{"type": "btn", "data": {"button": "right", "down": down}}]})
        vm.hold(0.15)


def act(vm, scene):
    vm.hold(1.5)
    vm.key("f1")
    vm.hold(0.6)

    if scene == "calculator":
        command(vm, "sysver", "MayDOS 1.0.0 compatibility tools")
        command(vm, "calc 1+2*3", "Result:\n7\n")
        command(vm, "calc 10/0", "Division by zero")
        command(vm, "calc", "may@mayos:~ $ calc")
        vm.hold(1.6)
        # Actual native 5-column keypad in the centered Calculator window.
        # 7 + 8 = 15; no direct modification of the guest's memory.
        for x, y in ((345, 324), (507, 522), (426, 324), (581, 522)):
            vm.click(x, y)
            vm.hold(0.65)
        vm.hold(2.2)
        return ["sysver", "calc precedence", "division by zero",
                "Calculator GUI", "real mouse clicks"]

    if scene == "minesweeper":
        command(vm, "minesweeper easy", "MayDOS Minesweeper ready")
        vm.hold(1.5)
        # 9x9 board: native window (122,88), grid origin (350,265).
        right_click(vm, 640, 553)  # flag before first click: guaranteed hidden
        vm.hold(1.4)
        vm.click(367, 282)         # guaranteed safe on the first reveal
        vm.hold(2.1)
        # Reset, then switch difficulty via native buttons.
        vm.click(302, 155)  # medium
        vm.hold(1.4)
        vm.click(180, 155)  # easy
        vm.hold(1.7)
        return ["Minesweeper GUI", "first safe click", "flag",
                "difficulty switch", "restart"]

    if scene == "maynote":
        command(vm, "write Documents/legacy.MayNote SGVsbG8K", "OK")
        command(vm, "notepad Documents/legacy.MayNote",
                "may@mayos:~ $ notepad Documents/legacy.MayNote")
        vm.hold(1.8)
        # This text is decoded from the actual legacy Base64 data.
        vm.key("a")
        vm.hold(0.9)
        vm.key("ctrl-s")
        vm.hold(1.3)
        vm.key("f1")
        command(vm, "cat Documents/legacy.MayNote", "SGVsbG8KYQ==")
        vm.hold(1.7)
        return ["create .MayNote", "GUI Base64 decode", "native text edit",
                "Ctrl+S save", "verify encoded file"]

    if scene == "extensions":
        # The original MayDOS import staging directory is created by ADD.
        command(vm, "add", "no eligible new data")
        command(vm, "write Documents/AddFiles/demo.txt HELLO-MAYDOS", "OK")
        command(vm, "add demo.txt",
                "MayDOS: imported into Documents/ThirdPartyFile")
        command(vm, "list", "\ndemo.txt\n")
        command(vm, "apps", "may@mayos:~ $ apps")
        vm.hold(1.8)
        vm.click(284, 354)   # first data-file row in native Extension window
        vm.hold(0.8)
        vm.click(567, 286)   # move selection to Trash (not permanent delete)
        vm.hold(1.5)
        vm.key("f1")
        command(vm, "list", "MayDOS: no imported data files")
        vm.hold(1.6)
        return ["MayFS staging file", "ADD", "LIST", "Extensions GUI",
                "select file", "move to Trash", "verify no imported data"]

    raise ValueError("unrecognized MayDOS recording scene")


def record(scene, args):
    output = args.output_dir / ("MayDOS-" + scene + "-real-QEMU.mp4")
    output.parent.mkdir(parents=True, exist_ok=True)
    args.output = output
    args.kernel = "custom"
    args.firmware = "bios"
    args.ovmf_code = args.ovmf_vars = None
    vm = Session(args)
    started = datetime.now(timezone.utc).isoformat()
    actions = []
    try:
        vm.start_recording()
        vm.hold(1.5)
        vm.key("home")
        vm.key("ret")
        vm.wait("MayOS: CPU long mode active (64-bit)", timeout=60)
        vm.wait("MayOS: desktop ready", timeout=30)
        actions = act(vm, scene)
        vm.hold(1.0)
        vm.finish_recording()
    finally:
        vm.close()

    if not output.exists() or output.stat().st_size < 15000 or vm.frames < args.fps * 10:
        raise AssertionError("real QEMU recording was empty or too short: " + str(output))
    probe = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height",
        "-of", "json", str(output)
    ], text=True))["streams"][0]
    if (probe["codec_name"] != "h264" or probe["width"] != 1024 or
            probe["height"] != 768):
        raise AssertionError("Unexpected video codec or size: " + repr(probe))
    report = {
        "scene": scene,
        "mayos_version": args.version,
        "source_commit": args.source_commit,
        "ISO_sha256": args.iso_hash,
        "video_sha256": sha(output),
        "frames": vm.frames,
        "fps": args.fps,
        "duration_seconds": round(vm.frames / args.fps, 2),
        "recorded_utc": started,
        "capture": "Unedited real QEMU framebuffer through QMP; no synthesized frames",
        "actions": actions,
    }
    output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS:", scene, report["duration_seconds"], "seconds", flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--fps", type=int, default=8)
    args = parser.parse_args()
    args.iso = args.iso.resolve()
    args.output_dir = args.output_dir.resolve()
    args.version = (Path(__file__).resolve().parents[1] / "VERSION").read_text().strip()
    assert re.fullmatch("[a-f0-9]{40}", args.source_commit)
    assert 4 <= args.fps <= 20
    assert args.iso.is_file()
    args.iso_hash = sha(args.iso)
    clips = []
    for scene in SCENES:
        clips.append(record(scene, args))
        assert sha(args.iso) == args.iso_hash, "ISO changed during recording"
    concat = args.output_dir / "MayDOS-all-four-real-QEMU.mp4"
    entries = args.output_dir / "concat.txt"
    entries.write_text("".join("file '" + str(clip) + "'\n" for clip in clips))
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "concat", "-safe", "0", "-i", str(entries),
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "22",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(concat)
    ], check=True)
    assert concat.stat().st_size > 50000
    (args.output_dir / "MayDOS-all-four-real-QEMU.json").write_text(
        json.dumps({"source_commit": args.source_commit,
                    "ISO_sha256": args.iso_hash,
                    "clips": [p.name for p in clips],
                    "combined_sha256": sha(concat)}, indent=2) + "\n")
    print("PASS: four independently recorded MayDOS scenes + combined MP4", flush=True)


if __name__ == "__main__":
    main()

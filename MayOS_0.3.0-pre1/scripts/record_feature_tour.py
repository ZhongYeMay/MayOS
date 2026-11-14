#!/usr/bin/env python3
"""Record a truthful, unedited QEMU feature walkthrough after every GUI update.

Includes both new settings and actual system functions. Frames are sampled from
the running guest by QMP and encoded with FFmpeg. No screenshot reconstruction.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import json
import re
import subprocess
from pathlib import Path

from record_boot import Session, sha, BUILD


def record(args):
    args.iso = args.iso.resolve()
    args.output = args.output.resolve()
    args.firmware = "bios"
    args.ovmf_code = args.ovmf_vars = None
    root = Path(__file__).resolve().parents[1]
    args.release = (root / "VERSION").read_text().strip()
    declared_version = (root / "kernel" / "version.h").read_text(encoding="utf-8")
    if f'#define MAYOS_VERSION "{args.release}"' not in declared_version:
        raise ValueError("VERSION and kernel/version.h mismatch; refuse mislabeled video")
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_commit):
        raise ValueError("Exact 40-character source commit required")
    if not args.iso.is_file():
        raise FileNotFoundError(args.iso)
    iso_hash = sha(args.iso)
    started = datetime.now(timezone.utc).isoformat()
    vm = Session(args)
    actions = ["real_BIOS_GRUB", "native_desktop"]
    disk = BUILD / (vm.tag + "-disk.img")
    try:
        vm.start_recording()
        # GRUB draws its menu on the VGA framebuffer; it may not echo entry
        # titles to ttyS0. Never infer boot success from a serial menu label.
        vm.hold(2.0)
        vm.key("home")  # first item is native MayKernel64
        if args.kernel == "linux":
            vm.key("down")  # second item is Linux x86_64 desktop in dual ISO
        vm.key("ret")
        vm.wait("MayOS: desktop ready", timeout=110)
        vm.hold(1.5)
        if args.kernel == "custom":
            vm.wait("MayOS: CPU long mode active (64-bit)")
        actions.append("native_x86_64_kernel" if args.kernel == "custom" else "real_Linux_userspace")

        # Exact approved monochrome Settings reference: open the top-left
        # MayOS menu, then dwell on About and the visible navigation items.
        vm.click(50, 16)
        vm.hold(2.2)
        vm.click(270, 301)                 # Display
        vm.hold(1.2)
        vm.click(270, 481)                 # About MayOS
        vm.hold(2.2)
        actions += ["Settings_reference", "Settings_navigation", "About_MayOS"]
        # In the exact screenshot's top bar, every icon has a real native panel.
        for x, label in ((777, "network_status_panel"),
                         (816, "notifications_panel"),
                         (855, "power_menu")):
            vm.click(x, 21)
            vm.hold(.85)
            vm.key("esc")
            vm.hold(.2)
            actions.append(label)

        vm.click(403, 710)                 # Files from the four-icon Dock
        vm.hold(1.3)
        vm.click(490, 212)                 # new folder modal
        vm.command("RecordedTour")
        vm.hold(1.1)
        actions.append("file_manager_create_folder")
        vm.click(370, 212)                 # new text file / editor
        vm.command("recorded.txt")
        vm.hold(1)
        actions.append("new_text_file_dialog")

        vm.key("f1")                       # real MayShell
        vm.hold(.8)
        offset = len(vm.text())
        vm.command("neofetch")
        vm.wait("Kernel:", offset=offset, timeout=25)
        vm.hold(1.5)
        vm.command("help")
        vm.hold(1.4)
        vm.command("ls")
        vm.hold(1.3)
        actions += ["MayShell", "kernel_info", "help_commands", "list_files"]
        vm.command("meow")
        vm.hold(1.7)
        vm.key("esc")
        actions.append("cat_easter_egg")

        vm.hold(1.5)
        vm.finish_recording()
    finally:
        vm.close()

    if sha(args.iso) != iso_hash:
        raise AssertionError("ISO changed while recording")
    if vm.frames < args.fps * 35 or args.output.stat().st_size < 30_000:
        raise AssertionError("QEMU MP4 is too short or empty")
    video_info = subprocess.check_output(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=codec_name,width,height,nb_frames",
         "-of", "json", str(args.output)], text=True)
    stream = json.loads(video_info)["streams"][0]
    if stream.get("codec_name") != "h264" or stream.get("width") != 1024 or stream.get("height") != 768:
        raise AssertionError(f"invalid video stream: {stream}")
    result = {
        "release_source_version": args.release,
        "source_commit": args.source_commit,
        "kernel": args.kernel,
        "iso": args.iso.name,
        "iso_sha256": iso_hash,
        "video": args.output.name,
        "video_sha256": sha(args.output),
        "frames": vm.frames,
        "fps": args.fps,
        "duration_seconds": vm.frames / args.fps,
        "recorded_utc": started,
        "capture": "Real running QEMU framebuffer via QMP; no reconstructed frames",
        "actions": actions,
        "qemu": subprocess.check_output([args.qemu, "--version"], text=True).splitlines()[0],
    }
    args.output.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, choices=("custom", "linux"))
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--fps", type=int, default=8)
    record(parser.parse_args())

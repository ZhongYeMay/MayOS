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
    args.release = "0.4.1-pre1"
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
        vm.wait("custom x86_64 kernel" if args.kernel == "linux" else "custom kernel", timeout=35)
        if args.kernel == "linux":
            vm.key("down")
        vm.key("ret")
        vm.wait("MayOS: desktop ready", timeout=110)
        vm.hold(1.5)
        if args.kernel == "custom":
            vm.wait("MayOS: CPU long mode active (64-bit)")
        actions.append("native_x86_64_kernel" if args.kernel == "custom" else "real_Linux_userspace")

        # Personalization settings: two themes, sixth accent, high contrast.
        vm.click(295, 236)
        vm.hold(.8)
        vm.click(440, 330)
        vm.hold(.8)
        vm.click(729, 549)
        vm.hold(.7)
        vm.click(550, 660)
        vm.hold(1.2)
        actions += ["light_theme", "sixth_accent_color", "high_contrast"]

        # Three wallpaper modes, 3 cursor sizes, desktop branding.
        vm.click(295, 380)
        vm.hold(1)
        vm.click(646, 335)                 # solid background
        vm.hold(.7)
        vm.click(822, 335)                 # grid background
        vm.hold(.9)
        vm.click(614, 481)                 # medium pointer
        vm.hold(.6)
        vm.click(768, 481)                 # large pointer
        vm.hold(.9)
        vm.click(599, 582)                 # hide branding
        vm.hold(1.2)
        actions += ["gradient_flat_grid_wallpapers", "pointer_sizes", "branding_toggle"]

        if args.kernel == "custom":
            from settings_smoke import saved_values, EXPECTED
            values = saved_values(disk)
            if values != EXPECTED:
                raise AssertionError(f"Video setting changes did not persist: {values!r}")
            before = len(vm.text())
            vm.q("system_reset")
            vm.wait("MayOS: desktop preferences applied", offset=before, timeout=60)
            vm.wait("MayOS: desktop ready", offset=before, timeout=60)
            vm.hold(1.5)
            vm.click(295, 380)
            vm.hold(1.2)
            actions.append("real_cold_boot_MayFS_settings_restoration")
        vm.click(450, 645)                 # reset all preferences
        vm.hold(1.2)
        actions.append("restore_default_settings")

        # Remaining settings sections and existing native apps.
        vm.click(295, 288)                 # Storage
        vm.hold(1.5)
        vm.click(295, 334)                 # Credits
        vm.hold(1.5)
        vm.click(295, 196)                 # About
        vm.hold(1.1)
        actions += ["storage_capacity", "open_source_credits", "about_kernel_info"]
        vm.click(948, 94)                  # close Settings
        vm.hold(.8)

        vm.click(55, 105)                  # real file manager
        vm.hold(1.3)
        vm.click(490, 212)                 # new folder modal
        vm.command("RecordedTour")
        vm.hold(1.1)
        actions.append("file_manager_create_folder")
        vm.click(370, 212)                 # new text file / editor
        vm.command("recorded.txt")
        vm.hold(1)
        actions.append("text_editor_open")

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

        if args.kernel == "custom":
            vm.click(55, 605)             # own native game launcher
            vm.hold(2.0)
            actions.append("MayKernel_native_game")
        else:
            vm.click(55, 525)             # Explorer and its limitations
            vm.hold(1.7)
            actions.append("Linux_Explorer")
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

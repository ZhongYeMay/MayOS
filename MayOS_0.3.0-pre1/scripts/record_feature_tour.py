#!/usr/bin/env python3
"""Record a truthful QEMU walkthrough of the *current* MayOS desktop UI.

Every scripted click targets current native input coordinates. Capture uses
real QMP frames, never screenshot reconstruction or static concept artwork.
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

        # Actual screenshot-matched four-icon Dock (Files/Settings/Terminal/Trash).
        # Every mouse coordinate corresponds to kernel/gui.c input hitboxes.
        vm.click(596, 662)                 # Settings Dock icon
        vm.hold(4.0)                       # Show screenshot-accurate About card
        actions.append("reference_About_MayOS_proportional_font")

        # Search field is genuinely interactive. Enter selects the first match;
        # Escape clears the filter so the sidebar is reachable again.
        vm.click(310, 230)
        vm.command("Display")
        vm.hold(2.0)
        actions.append("real_settings_search_and_display_navigation")
        vm.click(552, 330)                 # Light appearance tile
        vm.hold(1.3)
        vm.click(794, 330)                 # Dark appearance tile
        vm.hold(1.3)
        vm.key("esc")                      # Clear search and restore all sections
        actions.append("real_light_and_dark_appearance_switch")
        vm.click(315, 570)                 # About item on full-height sidebar
        vm.hold(3.0)
        actions.append("real_About_card_after_theme_change")

        # The General hub preserves previous controls and adds macOS-like
        # functional panels. Real pointer input is captured in QEMU frames.
        vm.click(315, 276)                 # About > General (compact view)
        vm.hold(.7)
        vm.click(598, 195)                 # General > Desktop & Pointer
        vm.hold(.9)
        vm.click(942, 117)                 # Back to General
        vm.hold(.6)
        actions.append("real_General_desktop_pointer_navigation")

        vm.click(600, 379)                 # General > Dock & Menu Bar
        vm.hold(1.0)
        vm.click(850, 238)                 # Large antialiased Dock icons
        vm.hold(1.1)
        vm.click(685, 322)                 # Hide running indicators
        vm.hold(.6)
        vm.click(691, 408)                 # Solid Dock, then restore glass
        vm.hold(.6)
        vm.click(555, 408)                 # Restore translucent Dock
        vm.click(550, 490)                 # Enable app-name hover
        vm.hold(.8)
        vm.click(690, 576)                 # Hide top status icons
        vm.hold(.6)
        vm.click(550, 576)                 # Restore status icons
        vm.click(942, 117)                 # Return to General
        actions += ["real_dock_size", "running_app_indicators",
                    "dock_surface_and_hover_names", "menu_bar_status_visibility"]

        vm.click(600, 446)                 # General > Accessibility
        vm.hold(.8)
        vm.click(688, 238)                 # High contrast
        vm.click(850, 330)                 # Large pointer
        vm.click(550, 426)                 # Reduce Dock movement
        vm.hold(.9)
        vm.click(942, 117)
        vm.click(600, 510)                 # Keyboard & Shortcuts
        vm.hold(1.0)
        vm.click(942, 117)
        actions += ["real_accessibility_contrast_pointer_and_motion",
                    "actual_keyboard_shortcuts_panel"]

        # Each reference sidebar label has its own meaningful page.
        for y, name in ((202, "general"),
                        (242, "display"),
                        (282, "sound"),
                        (322, "network"),
                        (362, "privacy_security"),
                        (402, "updates")):
            vm.click(315, y)
            vm.hold(1.0)
            actions.append("settings_" + name)
        vm.click(315, 569)                 # About at bottom
        vm.hold(3.0)
        vm.click(248, 132)                 # First traffic-light: close Settings
        vm.hold(2.2)
        actions.append("functional_settings_traffic_light_close")

        vm.click(539, 664)                 # Files from the redesigned Dock
        vm.hold(2.2)
        actions.append("real_file_manager")
        vm.click(370, 212)                 # New file dialogue
        vm.command("recorded.txt")
        vm.hold(1.0)
        actions.append("new_file_dialog")

        vm.key("f1")                       # Actual MayShell (not HTML terminal)
        vm.hold(.9)
        offset = len(vm.text())
        vm.command("neofetch")
        vm.wait("Kernel:", offset=offset, timeout=25)
        vm.hold(2.5)
        vm.command("help")
        vm.hold(2.0)
        vm.command("ls")
        vm.hold(1.7)
        actions += ["real_MayShell", "kernel_version", "help_commands", "file_listing"]
        if args.kernel == "custom":
            vm.command("meow")
            vm.hold(2.0)
            vm.key("esc")
            actions.append("native_cat_easter_egg")
        vm.hold(5.0)                       # Reliable >=35s reviewed video evidence
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
    if stream.get("codec_name") != "h264" or stream.get("width") != 1280 or stream.get("height") != 720:
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

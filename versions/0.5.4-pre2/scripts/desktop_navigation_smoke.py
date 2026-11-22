#!/usr/bin/env python3
"""Exercise the real desktop launcher and menu actions on both kernels."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import tempfile
import time

from PIL import Image, ImageChops

from dual_boot_smoke import BootVM


def exercise(args, disk, linux):
    label = "Linux64" if linux else "MayKernel64"
    with disk.open("xb") as output:
        output.truncate(8 * 1024 * 1024)
    vm = BootVM(iso=args.iso, disk=disk, tag=f"navigation-{label}",
                qemu=args.qemu, memory=512, timeout=25)

    def capture(state):
        shot = vm.shot(f"{label}-{state}")
        if args.screenshots:
            shutil.copyfile(shot, args.screenshots / shot.name)
        with Image.open(shot) as image:
            return image.convert("RGB")

    def different(first, second, box=None):
        # Coordinates in this test apply to relative guest input. Screenshots
        # use whichever firmware mode the ISO picked.
        diff = ImageChops.difference(first, second)
        return bool((diff.crop(box) if box else diff).getbbox())

    try:
        if linux:
            vm.wait("Linux 7.3.1 - desktop", timeout=55)
            vm.key("down")
            vm.key("ret")
        vm.wait("MayOS: desktop ready", timeout=90)
        idle = capture("desktop")
        vm.click(520, 674)  # Dock's application grid.
        vm.wait("MayOS: application launcher opened")
        launcher = capture("application-launcher")
        assert different(idle, launcher)
        vm.click(530, 386)  # Task Manager tile under Settings.
        vm.wait("MayOS: Task Manager opened")
        vm.click(1063, 111)  # Task Manager close.
        vm.click(50, 16)    # MayOS application menu.
        vm.click(70, 54)    # Open the launcher from the menu instead.
        before = len(vm.text)
        vm.keyboard_command("Explorer")
        vm.wait("MayOS: launcher application opened", offset=before)
        vm.click(1018, 123)  # Explorer close.

        vm.click(119, 16)  # Settings menu.
        vm.click(135, 55)  # General page.
        vm.move(1150, 591)
        time.sleep(.3)
        general = capture("settings-general")
        vm.click(930, 300)
        vm.keyboard_command("QA")
        vm.move(1150, 591)
        renamed = capture("settings-device-renamed")
        assert different(general, renamed), f"{label}: device rename did not change General"
        vm.click(276, 228)  # Search settings.
        vm.keyboard_command("display")
        vm.move(1150, 591)
        time.sleep(.3)
        display = capture("settings-display")
        assert different(renamed, display), f"{label}: settings search did not change page"
        before = len(vm.text)
        vm.click(553, 396)
        vm.wait("MayOS: display scaling changed", offset=before)
        vm.click(1020, 148)  # Close settings.

        vm.click(176, 16)  # File menu.
        vm.click(200, 121)  # New text file.
        vm.keyboard_command("menu-check.txt")
        time.sleep(.7)
        vm.click(600, 393)  # Select the new document in the third row.
        vm.click(233, 16)  # Edit menu.
        vm.click(251, 54)  # Copy selected file.
        vm.click(233, 16)
        vm.click(251, 118)  # Paste into folder.

        vm.click(119, 16)
        vm.click(137, 183)  # Personalization.
        vm.click(700, 227)  # Desktop preferences.
        vm.click(542, 304)  # Gradient, independent of a successful wallpaper download.
        vm.click(1020, 148)
        vm.click(290, 16)  # View menu.
        vm.click(309, 87)  # Switch to light.
        vm.move(1150, 590)
        light = capture("light-appearance")
        w, h = light.size
        assert max(light.getpixel((w // 20, h // 2))) >= 160, label
        vm.click(290, 16)
        vm.click(309, 87)  # Restore dark.
        vm.click(402, 16)  # Help menu.
        vm.click(418, 53)  # Shortcuts and commands.
        vm.wait("MayShell: type help to list commands.")
        before = len(vm.text)
        vm.keyboard_command("ls Documents")
        vm.wait("menu-check.txt-copy", offset=before)
        vm.click(345, 16)  # Window menu.
        vm.move(1150, 590)
        window = capture("window-menu")
        vm.click(364, 85)  # Maximize focused terminal.
        maximized = capture("maximized-terminal")
        assert different(window, maximized), label
        print(f"PASS: {label} launcher, Settings search and rename, file copy/paste, theme, window and Help", flush=True)
    finally:
        vm.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    args.iso = args.iso.resolve(strict=True)
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-navigation-") as directory:
        with ThreadPoolExecutor(max_workers=2) as pool:
            runs = [pool.submit(exercise, args, Path(directory) / name, linux)
                    for name, linux in (("native.img", False), ("linux.img", True))]
            for run in runs:
                run.result()


if __name__ == "__main__":
    main()

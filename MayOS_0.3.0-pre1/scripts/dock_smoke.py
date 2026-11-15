#!/usr/bin/env python3
"""Test the native MayOS Dock against a real QEMU guest, not a UI mock."""
from pathlib import Path
from PIL import Image, ImageChops
from smoke import VM, BUILD


def crop(name, region):
    return Image.open(BUILD / (name + ".png")).convert("RGB").crop(region)


def differs(a, b, region):
    return ImageChops.difference(crop(a, region), crop(b, region)).getbbox() is not None


def main():
    disk = BUILD / "test-dock.img"
    with disk.open("wb") as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = VM(disk, "test-dock")
    try:
        vm.shot("test-dock-default")
        vm.key("f1")
        vm.cmd("calc")
        vm.shot("test-dock-calculator-running")
        # Four pinned locations become three pinned + calculator + Trash.
        # The transient calculator icon is the fourth item in a 5-icon dock.
        left = (1024 - (5 * 57 + 16)) // 2
        icon_center = left + 18 + 3 * 57 + 20
        icon_y = 768 - 92 + 38
        assert differs("test-dock-default", "test-dock-calculator-running",
                       (300, 665, 740, 757)), "running application not visible in Dock"
        vm.click(icon_center, icon_y)   # focused -> minimize
        vm.shot("test-dock-calculator-minimized")
        assert differs("test-dock-calculator-running", "test-dock-calculator-minimized",
                       (280, 125, 750, 615)), "Dock click failed to minimize"
        # A minimized app stays running and must remain in the Dock.
        vm.click(icon_center, icon_y)  # minimized -> restore + focus
        vm.shot("test-dock-calculator-restored")
        assert differs("test-dock-calculator-minimized", "test-dock-calculator-restored",
                       (280, 125, 750, 615)), "Dock click failed to restore"
        # Close at the real native window titlebar controls.
        vm.click(287 + 450 - 24, 125 + 22)
        vm.shot("test-dock-calculator-closed")
        assert differs("test-dock-calculator-running", "test-dock-calculator-closed",
                       (290, 670, 745, 752)), "transient icon did not leave Dock"
        print("PASS: QEMU running-app Dock icon, minimize, restore, close and removal", flush=True)
    finally:
        vm.stop()


if __name__ == "__main__":
    main()

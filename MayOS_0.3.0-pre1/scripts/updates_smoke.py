#!/usr/bin/env python3
"""Real MayKernel64 QEMU test: Updates UI + unchanged top bar + blocked installer."""
from PIL import Image, ImageChops
from smoke import VM, BUILD

def crop(name, region):
    return Image.open(BUILD / (name + ".png")).convert("RGB").crop(region)

def main():
    disk = BUILD / "test-native-updates.img"
    with disk.open("wb") as f: f.truncate(8 * 1024 * 1024)
    vm = VM(disk, "test-native-updates")
    try:
        vm.click(50,16)
        vm.shot("test-updates-about-before")
        vm.click(237,455)  # Settings sidebar row 5 (Updates)
        vm.wait("MayOS: native Settings > Updates opened (offline catalog)")
        vm.shot("test-updates-offline")
        assert ImageChops.difference(
            crop("test-updates-about-before",(380,248,799,545)),
            crop("test-updates-offline",(380,248,799,545))
        ).getbbox(), "Updates page was not rendered"
        assert not ImageChops.difference(
            crop("test-updates-about-before",(150,0,1024,31)),
            crop("test-updates-offline",(150,0,1024,31))
        ).getbbox(), "Approved top status bar changed"
        vm.click(430,547)
        vm.wait("MayOS: update online check unavailable, no network request made")
        vm.click(615,547)
        vm.wait("MayOS: update install blocked (no A/B verified rollback)")
        vm.shot("test-updates-install-blocked")
        print("PASS: MayKernel native Updates page, unchanged top bar, "
              "offline warning and blocked unsafe installer",flush=True)
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

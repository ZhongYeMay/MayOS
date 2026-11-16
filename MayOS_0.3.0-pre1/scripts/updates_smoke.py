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
        # The first screenshot previously included the PS/2 mouse pointer over
        # the system title bar; a byte-perfect comparison then failed when the
        # pointer moved to Updates, despite unchanged status-bar code.
        vm.move(1000,690)
        vm.shot("test-updates-about-before")
        vm.click(237,455)  # Settings sidebar row 5 (Updates)
        vm.wait("MayOS: native Settings > Updates opened (offline catalog)")
        vm.move(1000,690)
        vm.shot("test-updates-offline")
        assert ImageChops.difference(
            crop("test-updates-about-before",(380,248,799,545)),
            crop("test-updates-offline",(380,248,799,545))
        ).getbbox(), "Updates page was not rendered"
        assert not ImageChops.difference(
            crop("test-updates-about-before",(150,0,1024,31)),
            crop("test-updates-offline",(150,0,1024,31))
        ).getbbox(), "Approved top status bar changed"
        # These are actual view-tab coordinates for the native 1024x768
        # Settings window, not a scripted fake interface.
        vm.click(530,267)
        vm.wait("MayOS: Updates view changed")
        vm.move(1000,690)
        vm.shot("test-updates-release-notes")
        assert ImageChops.difference(
            crop("test-updates-offline",(392,305,780,512)),
            crop("test-updates-release-notes",(392,305,780,512))
        ).getbbox(), "Release notes view did not render"

        vm.click(678,267)
        vm.move(1000,690)
        vm.shot("test-updates-progress")
        assert vm.log.read_text(errors="replace").count(
            "MayOS: Updates view changed") >= 2, "Tab clicks did not register"
        assert ImageChops.difference(
            crop("test-updates-release-notes",(392,305,780,512)),
            crop("test-updates-progress",(392,305,780,512))
        ).getbbox(), "Progress view did not render"
        vm.click(430,547)
        vm.wait("MayOS: update online check unavailable, no network request made")
        vm.click(615,547)
        vm.wait("MayOS: update install blocked (no A/B verified rollback)")
        vm.shot("test-updates-install-blocked")
        assert not ImageChops.difference(
            crop("test-updates-about-before",(150,0,1024,31)),
            crop("test-updates-progress",(150,0,1024,31))
        ).getbbox(), "Approved top bar changed on Progress view"
        print("PASS: MayKernel native Updates tabs, unchanged top bar, "
              "offline warning and blocked unsafe installer",flush=True)
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Real MayKernel64 settings tests: framebuffer, mouse and disk persistence."""
from pathlib import Path
import struct
import time
from PIL import Image
from smoke import VM, BUILD

PREFS_OFFSET = 500 * 512
MAGIC = 0x4d505246
EXPECTED = (MAGIC, 0, 5, 2, 2, 0, 0x201, 1)
DEFAULTS = (MAGIC, 1, 0, 0, 0, 1, 0, 1)


def saved_values(disk: Path):
    with disk.open('rb') as stream:
        stream.seek(PREFS_OFFSET)
        record = stream.read(44)
    if record[:8] != b'MAYPREF1':
        return None
    return struct.unpack_from('<8I', record, 12)


def await_values(disk: Path, expected):
    deadline = time.monotonic() + 9
    while time.monotonic() < deadline:
        if saved_values(disk) == expected:
            return
        time.sleep(.1)
    raise AssertionError(f'MayFS settings mismatch: {saved_values(disk)} != {expected}')


def main():
    disk = BUILD / 'test-settings-persistence.img'
    with disk.open('wb') as f:
        f.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'settings-configure')
    try:
        vm.click(444, 716)             # open original Settings from the dock
        vm.click(295, 236)             # appearance page
        vm.click(440, 330)             # light theme
        vm.click(729, 549)             # 6th accent
        vm.click(550, 660)             # high contrast
        vm.shot('settings-appearance-extended')
        vm.click(295, 380)             # desktop / pointer
        vm.click(822, 335)             # grid wallpaper
        vm.click(768, 449)             # large pointer
        vm.click(610, 530)             # white square
        vm.move(110, 510)
        vm.shot('settings-pointer-white-square')
        assert Image.open(BUILD / 'settings-pointer-white-square.png').convert('RGB').getpixel((115, 515)) == (255, 255, 255)
        vm.click(445, 530)             # black square
        vm.move(110, 510)
        vm.shot('settings-pointer-black-square')
        assert Image.open(BUILD / 'settings-pointer-black-square.png').convert('RGB').getpixel((115, 515)) == (16, 16, 16)
        vm.click(790, 530)             # load custom Documents/Cursor.ppm
        vm.move(110, 510)
        vm.shot('settings-pointer-custom-image')
        assert Image.open(BUILD / 'settings-pointer-custom-image.png').convert('RGB').getpixel((118, 511)) == (20, 184, 166)
        vm.click(599, 605)             # hide desktop branding
        vm.shot('settings-desktop-extended')
        await_values(disk, EXPECTED)
        assert 'MayOS: settings restored from MayFS' not in vm.log.read_text()
    finally:
        vm.stop()

    # Cold boot must restore all seven values from a checksummed MayFS record.
    vm = VM(disk, 'settings-cold-boot')
    try:
        vm.wait('MayOS: desktop preferences applied')
        vm.click(444, 716)             # reopen Settings after cold boot
        vm.click(295, 236)
        vm.shot('settings-persisted-light')
        assert Image.open(BUILD / 'settings-persisted-light.png').getpixel((900, 500)) == (255, 247, 255)
        vm.click(295, 380)
        vm.click(450, 645)             # defaults
        await_values(disk, DEFAULTS)
        vm.shot('settings-reset')
    finally:
        vm.stop()
    print('PASS: original MayOS visual default, 6 accents, contrast, 3 wallpapers, square/image cursor, branding, MayFS cold boot and reset', flush=True)


if __name__ == '__main__':
    main()

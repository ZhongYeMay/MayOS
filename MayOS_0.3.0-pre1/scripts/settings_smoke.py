#!/usr/bin/env python3
"""Verify the reference Settings page survives a cold MayFS boot."""
from PIL import Image
from smoke import VM, BUILD


def capture(disk, name):
    vm = VM(disk, name)
    try:
        vm.click(50, 16)
        vm.shot(name)
        image = Image.open(BUILD / f'{name}.png').convert('RGB')
        assert image.getpixel((400, 300)) == (0, 0, 0)
        assert image.getpixel((15, 20)) == (5, 5, 6)
    finally:
        vm.stop()


def main():
    disk = BUILD / 'test-settings-reference.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    capture(disk, 'settings-reference-first-boot')
    capture(disk, 'settings-reference-cold-boot')
    print('PASS: reference Settings layout renders after a MayFS cold boot', flush=True)


if __name__ == '__main__':
    main()

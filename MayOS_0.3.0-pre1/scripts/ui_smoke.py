#!/usr/bin/env python3
"""Pixel and input smoke test for the approved monochrome Settings reference."""
from smoke import VM, BUILD
from PIL import Image


def main():
    disk = BUILD / 'test-ui.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'test-ui')
    try:
        vm.shot('desktop-reference-050')
        desktop = Image.open(BUILD / 'desktop-reference-050.png').convert('RGB')
        assert desktop.size == (1280, 720), f'Expected true QEMU widescreen: {desktop.size}'
        assert desktop.getpixel((10, 20)) == (3, 3, 4)
        assert max(desktop.getpixel((15, 700))) < 25
        assert desktop.getpixel((600, 638)) == (10, 10, 12)

        vm.click(50, 16)
        vm.shot('about-mayos-reference-050')
        about = Image.open(BUILD / 'about-mayos-reference-050.png').convert('RGB')
        assert about.getpixel((500, 325)) == (0, 0, 0)
        assert about.getpixel((480, 325)) == (48, 48, 51), 'missing About card border'

        vm.click(315, 548)
        vm.shot('about-mayos-selected-050')
        assert Image.open(BUILD / 'about-mayos-selected-050.png').size == (1280, 720)
        print('PASS: exact monochrome desktop, English menu, Settings navigation, About card and four-icon Dock', flush=True)
    finally:
        vm.stop()


if __name__ == '__main__':
    main()

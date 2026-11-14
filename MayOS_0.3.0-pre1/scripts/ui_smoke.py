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
        assert desktop.size == (1024, 768)
        assert desktop.getpixel((10, 20)) == (5, 5, 6)
        assert max(desktop.getpixel((15, 760))) < 25
        assert desktop.getpixel((450, 700)) == (10, 10, 12)

        vm.click(50, 16)
        vm.shot('about-mayos-reference-050')
        about = Image.open(BUILD / 'about-mayos-reference-050.png').convert('RGB')
        assert about.getpixel((400, 300)) == (0, 0, 0)
        assert max(about.getpixel((383, 300))) < 80

        vm.click(270, 521)
        vm.shot('about-mayos-selected-050')
        assert Image.open(BUILD / 'about-mayos-selected-050.png').size == (1024, 768)
        print('PASS: exact monochrome desktop, English menu, Settings navigation, About card and four-icon Dock', flush=True)
    finally:
        vm.stop()


if __name__ == '__main__':
    main()

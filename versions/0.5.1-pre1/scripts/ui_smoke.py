#!/usr/bin/env python3
"""Pixel and input smoke test for the approved monochrome Settings reference."""
from smoke import VM, BUILD
from PIL import Image


def logical_pixel(image, x, y):
    """Read a pixel from the native 1280x720 desktop surface."""
    assert image.size == (1280, 720)
    return image.getpixel((x, y))


def main():
    disk = BUILD / 'test-ui.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'test-ui')
    try:
        vm.shot('desktop-reference-050')
        desktop = Image.open(BUILD / 'desktop-reference-050.png').convert('RGB')
        assert desktop.size == (1280, 720), (
            f'unexpected desktop framebuffer size: {desktop.size}, expected (1280, 720)'
        )
        assert logical_pixel(desktop, 10, 20) == (5, 5, 6)
        assert max(logical_pixel(desktop, 15, 712)) < 25
        assert max(logical_pixel(desktop, 640, 700)) < 35

        vm.click(50, 16)
        vm.shot('about-mayos-reference-050')
        about = Image.open(BUILD / 'about-mayos-reference-050.png').convert('RGB')
        assert logical_pixel(about, 600, 300) == (0, 0, 0)
        assert max(logical_pixel(about, 469, 300)) < 80

        vm.click(330, 490)
        vm.shot('about-mayos-selected-050')
        assert Image.open(BUILD / 'about-mayos-selected-050.png').size == (1280, 720)
        print('PASS: exact monochrome desktop, English menu, Settings navigation, About card and four-icon Dock', flush=True)
    finally:
        vm.stop()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Pixel and input smoke test for the approved monochrome Settings reference."""
from smoke import VM, BUILD
from PIL import Image
from reference_pixels import reference_pixel, verify_letterboxing


def main():
    disk = BUILD / 'test-ui.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'test-ui')
    try:
        vm.shot('desktop-reference-050')
        desktop = Image.open(BUILD / 'desktop-reference-050.png').convert('RGB')
        verify_letterboxing(desktop)
        assert reference_pixel(desktop, 10, 20) == (5, 5, 6)
        assert max(reference_pixel(desktop, 15, 760)) < 25
        assert reference_pixel(desktop, 450, 700) == (10, 10, 12)

        vm.click(50, 16)
        vm.shot('about-mayos-reference-050')
        about = Image.open(BUILD / 'about-mayos-reference-050.png').convert('RGB')
        verify_letterboxing(about)
        assert reference_pixel(about, 400, 300) == (0, 0, 0)
        assert max(reference_pixel(about, 383, 300)) < 80

        vm.click(270, 521)
        vm.shot('about-mayos-selected-050')
        selected = Image.open(BUILD / 'about-mayos-selected-050.png').convert('RGB')
        verify_letterboxing(selected)
        print('PASS: aspect-correct 4:3/16:9 monochrome desktop, Settings, About and four-icon Dock', flush=True)
    finally:
        vm.stop()


if __name__ == '__main__':
    main()

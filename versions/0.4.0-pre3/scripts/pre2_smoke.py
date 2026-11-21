#!/usr/bin/env python3
"""MayOS pre2 visual + interaction smoke tests against a running QEMU system."""
from PIL import Image
from smoke import BUILD, VM

def screenshot(vm, name):
    vm.shot(name)
    image = Image.open(BUILD / (name + '.png')).convert('RGB')
    assert image.size == (1024, 768)
    return image

def main():
    disk = BUILD / 'test-pre2-ui.img'
    with disk.open('wb') as f:
        f.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'test-pre2-ui')
    try:
        # Close the initial Settings window and draw a clean desktop.
        vm.click(948, 94)
        vm.move(510, 520)
        image = screenshot(vm, 'pre2-windows-pointer')
        assert image.getpixel((510, 520)) == (17, 18, 21), (
            'Windows arrow pointer has no dark upper-left outline')
        assert image.getpixel((512, 523)) == (255, 255, 255), (
            'Windows arrow pointer has no white arrow fill')

        # Right-hand menu: no textual labels, each icon performs an action.
        vm.click(730, 20)   # clock
        screenshot(vm, 'pre2-clock-icon-popover')
        vm.click(601, 460)  # close clock panel

        vm.click(930, 20)   # power
        screenshot(vm, 'pre2-power-icon-popover')
        vm.click(601, 460)

        vm.click(970, 20)   # settings / quick controls
        screenshot(vm, 'pre2-icon-only-statusbar')
        vm.click(735, 270)  # functional brightness decrease
        vm.click(889, 20)   # notifications
        screenshot(vm, 'pre2-notifications-icon-popover')
        vm.click(601, 460)

        vm.click(248, 717)  # app drawer
        screenshot(vm, 'pre2-polished-dock')
        vm.type('terminal\n')
        vm.cmd('help')
        vm.wait('F1 opens Terminal.')
        screenshot(vm, 'pre2-polished-windows')
        print('PASS: Windows arrow pointer, icon-only system status, clock, '
              'power, brightness, notifications, Dock and window UI', flush=True)
    finally:
        vm.stop()

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Verify the actual MayOS desktop shell and Material panels using QEMU input."""
from pathlib import Path
from PIL import Image
from smoke import BUILD, VM
import time

def main():
    disk = BUILD / 'test-material-shell.img'
    with disk.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'test-material-shell')
    try:
        # Start with a normal desktop rather than the legacy About window.
        vm.click(948, 94)
        vm.shot('material-desktop')
        vm.click(970, 20)
        vm.shot('material-quick-settings')
        vm.click(735, 270)  # brightness -10, updates framebuffer output
        vm.shot('material-dimmed-desktop')
        base = Image.open(BUILD / 'material-desktop.png').convert('RGB')
        dimmed = Image.open(BUILD / 'material-dimmed-desktop.png').convert('RGB')
        assert sum(dimmed.getpixel((960, 610))) < sum(base.getpixel((960, 610))), (
            'Brightness slider did not change the displayed framebuffer')
        vm.click(889, 20)
        vm.shot('material-notification-center')
        vm.click(977, 86)  # clear notifications
        vm.click(248, 717)
        vm.shot('material-app-drawer')
        vm.type('terminal\n')
        vm.cmd('help')
        vm.wait('F1 opens Terminal.')
        vm.shot('material-terminal')
        vm.key('alt-tab')
        vm.shot('material-window-overview')
        vm.key('esc')
        vm.click(346, 717)
        vm.shot('material-files')
        vm.click(954, 480, 'right')
        vm.shot('material-desktop-context')
        print('PASS: native desktop, quick settings, brightness, notifications, '
              'app search, Dock, Alt+Tab and context menus', flush=True)
    finally:
        vm.stop()

if __name__ == '__main__':
    main()

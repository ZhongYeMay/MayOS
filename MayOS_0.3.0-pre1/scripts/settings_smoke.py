#!/usr/bin/env python3
"""Verify the reference Settings page survives a cold MayFS boot."""
from PIL import Image
from smoke import VM, BUILD
from ui_smoke import logical_pixel


def capture(disk, name):
    vm = VM(disk, name)
    try:
        vm.click(50, 16)
        vm.shot(name)
        image = Image.open(BUILD / f'{name}.png').convert('RGB')
        assert image.size == (1280, 720)
        assert logical_pixel(image, 600, 300) == (0, 0, 0)
        assert logical_pixel(image, 15, 20) == (5, 5, 6)
    finally:
        vm.stop()


def main():
    disk = BUILD / 'test-settings-reference.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    capture(disk, 'settings-reference-first-boot')
    capture(disk, 'settings-reference-cold-boot')
    vm = VM(disk, 'settings-display-controls')
    try:
        vm.click(50, 16)
        vm.click(330, 301)  # Display in the native 1280x720 sidebar.
        vm.shot('settings-display-auto')
        vm.click(817, 386)  # Fit scaling.
        vm.wait('MayOS: display scaling changed')
        vm.shot('settings-display-fit')
        vm.click(553, 386)  # Restore automatic matching.
        vm.shot('settings-display-restored')
    finally:
        vm.stop()
    print('PASS: Settings layout, detected display details and live scaling controls', flush=True)
    # Real MayKernel/QEMU interaction: verify both shell and Settings route
    # to the same native, read-only local manifest parser. This test uses an
    # isolated throwaway MayFS image, not an install image.
    update_disk = BUILD / 'test-settings-update.img'
    with update_disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = VM(update_disk, 'settings-update-check')
    try:
        vm.key('f1')
        vm.cmd('update check')
        vm.wait('No local manifest in Documents/Update.may')
        vm.cmd('write Documents/Update.may invalid-metadata')
        vm.wait('OK')
        vm.cmd('update check')
        vm.wait('Manifest invalid or incompatible (no changes made)')
        vm.click(50, 16)   # MayOS top menu opens native Settings
        vm.click(300, 452) # Updates tab in 1280x720 window
        vm.shot('settings-updates-local-check')
        vm.click(552, 369) # Check local manifest button
        vm.wait('MayOS: local update manifest checked')
        vm.shot('settings-updates-invalid-manifest')
        vm.click(780, 369) # Verify image while manifest is invalid: must refuse
        vm.wait('MayOS: local update image integrity checked')
        vm.shot('settings-updates-verify-refused')
    finally:
        vm.stop()
    print('PASS: native Updates page, shell commands and invalid manifest safety', flush=True)


if __name__ == '__main__':
    main()

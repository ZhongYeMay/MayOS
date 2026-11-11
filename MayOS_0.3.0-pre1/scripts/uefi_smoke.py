#!/usr/bin/env python3
"""Boot the real hybrid ISO under x86_64 OVMF, with private disks and VARS.

No QEMU -kernel shortcut is used. The default run tests custom, Linux desktop,
and Linux rescue entries through the same EFI GRUB loader with Secure Boot off.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from dual_boot_smoke import BootVM, BUILD, entries


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()



def verify_linux_desktop(vm):
    """Validate real desktop evidence without relying on lossy serial startup lines.

    The GUI writes backend readiness to /run/mayos-desktop.log before it
    announces "MayOS: desktop ready". In some OVMF boots the nonblocking
    /dev/ttyS0 output loses the earlier line while the desktop stays active.
    Query the guest log and process through the independent serial shell;
    never accept the GUI marker alone as evidence of a working backend.
    """
    vm.wait('MayOS: desktop ready')
    vm.wait('MayOS-Linux# ')
    vm.shell_check(
        'test -s /run/mayos-desktop.log '
        '&& grep -Fq "MayOS Linux desktop: backend ready;" /run/mayos-desktop.log '
        '&& test -s /run/mayos-desktop.pid '
        '&& test "$(cat /proc/$(cat /run/mayos-desktop.pid)/comm)" = mayos-desktop',
        'UEFI_BACKEND_READY')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=BUILD / 'mayos-dual.iso')
    parser.add_argument('--qemu', default=os.environ.get('QEMU64', 'qemu-system-x86_64'))
    parser.add_argument('--ovmf-code', type=Path,
                        default=os.environ.get('OVMF_CODE', '/usr/share/OVMF/OVMF_CODE_4M.fd'))
    parser.add_argument('--ovmf-vars', type=Path,
                        default=os.environ.get('OVMF_VARS', '/usr/share/OVMF/OVMF_VARS_4M.fd'))
    parser.add_argument('--memory', type=int, default=512)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--custom-only', action='store_true',
                        help='test a custom-only ISO before Linux payloads are ready')
    args = parser.parse_args()
    for path in (args.iso, args.ovmf_code, args.ovmf_vars):
        if not path.is_file():
            parser.error(f'missing file: {path}')
    if args.memory < 256:
        parser.error('use at least 256 MiB for the EFI memory-map regression')
    template_hash = digest(args.ovmf_vars)
    disk = BUILD / 'test-uefi-disk.img'
    with disk.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    tests = []

    def start(tag):
        variables = BUILD / f'{tag}-vars.fd'
        shutil.copyfile(args.ovmf_vars, variables)
        return BootVM(iso=args.iso.resolve(), disk=disk, tag=tag, qemu=args.qemu,
                      memory=args.memory, timeout=args.timeout,
                      ovmf_code=args.ovmf_code.resolve(), ovmf_vars=variables)

    vm = start('test-uefi-custom')
    try:
        if not args.custom_only:
            vm.wait('custom kernel')
            vm.key('home')  # stop the countdown and select the first entry
            time.sleep(.5)
            vm.shot('test-uefi-menu')
            vm.key('ret')
        vm.wait('MayOS: desktop ready')
        assert 'using Multiboot memory map' in vm.text, vm.text
        memory = re.search(r'available memory MiB=(\d+)', vm.text)
        assert memory and int(memory[1]) > 128, 'EFI RAM was truncated at the first memory-map hole'
        vm.key('f1')
        vm.keyboard_command('memtest')
        vm.wait('memtest: PASS (allocate, isolate, write, free)')
        vm.keyboard_command('write uefi.txt uefi-mayfs-preserved')
        vm.keyboard_command('cat uefi.txt')
        vm.wait('\nuefi-mayfs-preserved\n')
        assert entries(disk)['uefi.txt'][3] == 'uefi-mayfs-preserved'
        vm.shot('test-uefi-custom-desktop')
        tests += ['uefi_grub_custom', 'uefi_framebuffer', 'uefi_memory_map',
                  'uefi_custom_allocator', 'uefi_ps2_keyboard', 'uefi_mayfs_write']
        print('PASS: OVMF -> EFI GRUB -> 32-bit custom desktop; memory map, allocation, input and MayFS', flush=True)
    finally:
        vm.stop()

    vm = start('test-uefi-cold')
    try:
        vm.wait('MayOS: desktop ready')
        vm.wait('MayOS: MayFS mounted')
        vm.key('f1')
        vm.keyboard_command('cat uefi.txt')
        vm.wait('\nuefi-mayfs-preserved\n')
        tests.append('uefi_mayfs_cold_boot')
        print('PASS: independent UEFI cold boot reads persisted custom-kernel file', flush=True)
    finally:
        vm.stop()

    before_linux = digest(disk)
    release = None
    if not args.custom_only:
        manifest = json.loads((BUILD / 'linux/manifest.json').read_text())
        release = manifest['kernel_release']
        if re.fullmatch(r'[0-9A-Za-z._+\-]+', release) is None:
            raise ValueError('invalid kernel release in manifest')
        vm = start('test-uefi-linux')
        try:
            vm.wait('desktop (x86_64)')
            vm.key('home')
            vm.key('down')
            vm.key('ret')
            verify_linux_desktop(vm)
            vm.shell_check(f'test "$(uname -r)" = "{release}" && test -d /sys/firmware/efi', 'UEFI_KERNEL')
            vm.shell_check('test -c /dev/fb0 && test -s /run/mayos-desktop.pid '
                           '&& test "$(cat /proc/$(cat /run/mayos-desktop.pid)/comm)" = mayos-desktop', 'UEFI_GUI_PROCESS')
            vm.shell_check('! grep -E "^/dev/(sd|hd|vd)" /proc/mounts', 'UEFI_NO_DISK_MOUNTS')
            vm.key('f1')
            vm.keyboard_command('neofetch')
            vm.wait('Kernel: Linux ' + release)
            vm.keyboard_command('write uefi.txt uefi-linux-desktop-input')
            vm.keyboard_command('cat uefi.txt')
            vm.wait('\nuefi-linux-desktop-input\n')
            vm.shot('test-uefi-linux-desktop')
            vm.shutdown()
            tests += ['uefi_grub_linux', 'linux_efi_runtime', 'linux_gop_framebuffer',
                      'linux_desktop_process', 'linux_desktop_keyboard', 'uefi_linux_shutdown']
            print(f'PASS: EFI GRUB -> Linux {release}; real framebuffer desktop, input, serial shell and shutdown', flush=True)
        finally:
            vm.stop()

        vm = start('test-uefi-rescue')
        try:
            vm.wait('rescue shell (x86_64)')
            vm.key('home')
            vm.key('down')
            vm.key('down')
            vm.key('ret')
            vm.wait('MayOS-Linux# ')
            vm.shell_check('grep -q "mayos.desktop=0" /proc/cmdline '
                           '&& test -d /sys/firmware/efi && test ! -f /run/mayos-desktop.pid', 'UEFI_RESCUE')
            vm.keyboard_command('touch /tmp/uefi-rescue-keyboard')
            vm.shell_check('test -f /tmp/uefi-rescue-keyboard', 'UEFI_RESCUE_KEYBOARD')
            vm.shot('test-uefi-linux-rescue')
            vm.shutdown()
            tests.append('uefi_linux_rescue_shell')
            print('PASS: EFI rescue entry starts a usable shell without the desktop process', flush=True)
        finally:
            vm.stop()
        assert digest(disk) == before_linux, 'Linux modified the dedicated MayFS test disk'
        tests.append('uefi_linux_preserves_mayfs')

    assert digest(args.ovmf_vars) == template_hash, 'OVMF variable template was changed'
    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'iso': args.iso.name, 'iso_sha256': digest(args.iso),
        'firmware': 'x86_64 OVMF; Secure Boot disabled',
        'ovmf_code_sha256': digest(args.ovmf_code),
        'ovmf_vars_template_sha256': template_hash,
        'private_vars_per_vm': True, 'memory_mib': args.memory,
        'kernel_release': release, 'tests': tests,
        'qemu': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
        'mayfs_sha256_before_linux': before_linux, 'mayfs_sha256_after_linux': digest(disk),
    }
    name = 'uefi-custom-test-results.json' if args.custom_only else 'uefi-test-results.json'
    (BUILD / name).write_text(json.dumps(report, indent=2) + '\n')
    print('All requested UEFI tests passed.', flush=True)


if __name__ == '__main__':
    main()

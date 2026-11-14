#!/usr/bin/env python3
"""Repeated BIOS/UEFI GRUB boot + MayFS cold-read check on a private disk.

This is a short regression soak, not proof of production-grade durability.
It uses a real ISO and QEMU -cdrom, never QEMU -kernel, and never opens
build/mayos-disk.img. Every VM and UEFI variable store is independent.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

from dual_boot_smoke import BootVM, BUILD, entries


def sha256(path: Path) -> str:
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=BUILD / 'mayos.iso')
    parser.add_argument('--cycles', type=int, default=3,
                        help='cold boots per firmware; default is 3')
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--ovmf-code', type=Path,
                        default=Path('/usr/share/OVMF/OVMF_CODE_4M.fd'))
    parser.add_argument('--ovmf-vars', type=Path,
                        default=Path('/usr/share/OVMF/OVMF_VARS_4M.fd'))
    args = parser.parse_args()
    if not 2 <= args.cycles <= 20:
        parser.error('--cycles must be between 2 and 20')
    for path in (args.iso, args.ovmf_code, args.ovmf_vars):
        if not path.is_file():
            parser.error(f'missing required file: {path}')

    disk = BUILD / 'test-phase1-soak-disk.img'
    with disk.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    marker = 'phase-one-bios-uefi-cold-boot-ok'
    filename = 'phase1.txt'
    iso_hash = sha256(args.iso)
    template_hash = sha256(args.ovmf_vars)
    samples = []
    start = time.monotonic()
    expected_disk_hash = None

    for firmware in ('bios', 'uefi'):
        for index in range(args.cycles):
            tag = f'phase1-{firmware}-boot-{index + 1}'
            extras = {}
            if firmware == 'uefi':
                independent_vars = BUILD / f'{tag}-vars.fd'
                shutil.copyfile(args.ovmf_vars, independent_vars)
                extras = dict(ovmf_code=args.ovmf_code.resolve(),
                              ovmf_vars=independent_vars.resolve())
            before = sha256(disk)
            vm = BootVM(iso=args.iso.resolve(), disk=disk, tag=tag,
                        qemu=args.qemu, memory=512, timeout=120, **extras)
            try:
                vm.wait('MayOS: CPU long mode active (64-bit)')
                vm.wait('MayOS: desktop ready')
                if expected_disk_hash is not None:
                    vm.wait('MayOS: MayFS mounted')
                vm.key('f1')
                if expected_disk_hash is None:
                    vm.keyboard_command(f'write {filename} {marker}')
                offset = len(vm.text)
                vm.keyboard_command(f'cat {filename}')
                vm.wait(f'\n{marker}\n', offset=offset)
                if index == 0:
                    vm.shot(f'phase1-{firmware}-desktop')
            finally:
                vm.stop()

            current = entries(disk)
            assert current[filename][3] == marker, 'MayFS data changed after cold boot'
            after = sha256(disk)
            if expected_disk_hash is not None:
                assert before == after == expected_disk_hash, (
                    f'unexpected disk write during read-only {firmware} boot {index + 1}')
            else:
                assert before != after, 'First MayFS write did not change disk'
                expected_disk_hash = after
            samples.append({'firmware': firmware, 'cold_boot': index + 1,
                            'disk_sha256': after, 'serial_log': f'{tag}.log'})
            print(f'PASS: {firmware} boot {index + 1}/{args.cycles}; '
                  f'cold read and disk integrity', flush=True)

    assert sha256(args.ovmf_vars) == template_hash, 'Original OVMF VARS template was modified'
    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'iso': args.iso.name, 'iso_sha256': iso_hash,
        'firmware_boots': args.cycles * 2,
        'ovmf_code_sha256': sha256(args.ovmf_code),
        'ovmf_vars_template_sha256': template_hash,
        'disk_sha256_after_write': expected_disk_hash,
        'duration_seconds': round(time.monotonic() - start, 2),
        'qemu': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
        'boots': samples,
    }
    (BUILD / 'phase1-soak-test-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: repeated BIOS and UEFI cold boots preserve MayFS without writes', flush=True)


if __name__ == '__main__':
    run()

#!/usr/bin/env python3
"""Real ISO memory and MayFS stress. Uses private QEMU disk and OVMF VARS.

This is bounded stress plus idle availability, NOT a long-run endurance or
power-loss recovery certification. Normal ISO; no synthetic diagnostic image.
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


def digest(path: Path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def command_checked(vm: BootVM, command: str, expected: str):
    offset = len(vm.text)
    vm.keyboard_command(command)
    vm.wait(expected, offset=offset, timeout=90)


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=BUILD / 'mayos.iso')
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--writes', type=int, default=10)
    parser.add_argument('--mem-rounds', type=int, default=3)
    parser.add_argument('--idle-seconds', type=float, default=15)
    parser.add_argument('--ovmf-code', type=Path,
                        default=Path('/usr/share/OVMF/OVMF_CODE_4M.fd'))
    parser.add_argument('--ovmf-vars', type=Path,
                        default=Path('/usr/share/OVMF/OVMF_VARS_4M.fd'))
    args = parser.parse_args()
    if not 1 <= args.writes <= 25:
        parser.error('writes must be in [1, 25]')
    if not 1 <= args.mem_rounds <= 20:
        parser.error('mem-rounds must be in [1, 20]')
    if not 0 <= args.idle_seconds <= 900:
        parser.error('idle-seconds must be in [0, 900]')
    for path in (args.iso, args.ovmf_code, args.ovmf_vars):
        if not path.is_file():
            parser.error('missing file: ' + str(path))

    disk = BUILD / 'test-phase1-memory-filesystem.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    expected = {}
    began = time.monotonic()
    samples = []
    vm = BootVM(iso=args.iso.resolve(), disk=disk, tag='phase1-stress-bios',
                qemu=args.qemu, memory=512, timeout=120)
    try:
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        for i in range(args.mem_rounds):
            command_checked(vm, 'memstress', 'memstress: PASS')
            command_checked(vm, 'memtest', 'memtest: PASS')
            samples.append({'test': 'memory', 'round': i + 1, 'result': 'PASS'})
        for i in range(args.writes):
            name = f'stress{i:02d}.txt'
            value = f'mayfs-phase1-round-{i:02d}-persisted'
            command_checked(vm, f'write Documents/{name} {value}', '\\nOK\\n')
            expected[name] = value
            actual = entries(disk)
            assert actual[name][3] == value, f'Incorrect MayFS contents after write {i}'
            samples.append({'test': 'mayfs_write', 'index': i, 'file': name, 'result': 'PASS'})
        # A GUI that stops responding during idle is not counted as stable.
        start_idle = time.monotonic()
        while time.monotonic() - start_idle < args.idle_seconds:
            assert vm.proc.poll() is None, 'QEMU guest quit during idle'
            assert vm.q('query-status')['status'] == 'running', 'Guest not running'
            time.sleep(1)
        command_checked(vm, 'cat Documents/stress00.txt',
                        '\\n' + expected['stress00.txt'] + '\\n')
        vm.shot('phase1-stress-desktop')
    finally:
        vm.stop()
    disk_after_bios = digest(disk)
    assert all(entries(disk)[k][3] == v for k, v in expected.items())

    independent_vars = BUILD / 'phase1-stress-vars.fd'
    shutil.copyfile(args.ovmf_vars, independent_vars)
    vars_hash = digest(args.ovmf_vars)
    vm = BootVM(iso=args.iso.resolve(), disk=disk, tag='phase1-stress-uefi',
                qemu=args.qemu, memory=512, timeout=120,
                ovmf_code=args.ovmf_code.resolve(),
                ovmf_vars=independent_vars.resolve())
    try:
        vm.wait('MayOS: desktop ready')
        vm.wait('MayOS: MayFS mounted')
        vm.key('f1')
        command_checked(vm, 'memstress', 'memstress: PASS')
        for k in ('stress00.txt', f'stress{args.writes - 1:02d}.txt'):
            command_checked(vm, f'cat Documents/{k}', '\\n' + expected[k] + '\\n')
    finally:
        vm.stop()
    assert digest(disk) == disk_after_bios, 'UEFI memory and file reads modified MayFS disk'
    assert digest(args.ovmf_vars) == vars_hash, 'OVMF VARS template changed'
    assert all(entries(disk)[k][3] == v for k, v in expected.items())
    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'iso_sha256': digest(args.iso),
        'firmware': ['bios', 'uefi'],
        'memory_test_rounds_bios': args.mem_rounds,
        'memory_test_rounds_uefi': 1,
        'allocator_internal_rounds_per_memstress': 16,
        'files_written': args.writes,
        'idle_seconds': args.idle_seconds,
        'disk_sha256_after_bios_and_uefi': disk_after_bios,
        'duration_seconds': round(time.monotonic() - began, 2),
        'qemu': subprocess.check_output([args.qemu, '--version'],
                                        text=True).splitlines()[0],
        'samples': samples,
    }
    (BUILD / 'phase1-memory-filesystem-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print(f'PASS: {args.mem_rounds + 1} memstress commands, '
          f'{args.writes} MayFS writes, {args.idle_seconds}s idle, '
          f'cold UEFI read-back and unchanged disk SHA-256', flush=True)


if __name__ == '__main__':
    run()

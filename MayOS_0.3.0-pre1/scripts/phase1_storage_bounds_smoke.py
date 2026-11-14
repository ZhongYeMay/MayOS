#!/usr/bin/env python3
"""MayFS guard regression on foreign backup contents and exact ATA capacity.

All disks are disposable images created beneath build/. The test proves
single-slot fallback never destroys unknown data adjacent to a valid MayFS,
and that capacity checks refuse I/O beyond a 512-sector legacy disk.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil

from dual_boot_smoke import BootVM, BUILD
from smoke import entries

SECTOR = 512
SNAPSHOT = 512 * SECTOR


def digest(data: bytes):
    return hashlib.sha256(data).hexdigest()


def file_hash(path: Path):
    return digest(path.read_bytes())


def boot(disk: Path, iso: Path, tag: str):
    return BootVM(iso=iso.resolve(), disk=disk, tag=tag,
                  qemu='qemu-system-x86_64', memory=512, timeout=90)


def command(vm, command_text: str, expected: str):
    offset = len(vm.text)
    vm.keyboard_command(command_text)
    vm.wait(expected, offset=offset, timeout=90)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=BUILD / 'mayos.iso')
    args = parser.parse_args()
    if not args.iso.is_file():
        parser.error(f'ISO not found: {args.iso}')

    seed = BUILD / 'test-phase1-bounds-seed.img'
    with seed.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    vm = boot(seed, args.iso, 'phase1-bounds-seed')
    try:
        vm.wait('MayOS: desktop ready')
    finally:
        vm.stop()
    assert entries(seed)['Welcome.txt'][3].startswith('Welcome to MayOS!')
    original = seed.read_bytes()
    assert original[:8] == b'MAYFS001'

    # Scenario A: valid legacy primary, but backup sectors belong to another
    # unknown format. The entire foreign range must remain byte-for-byte intact.
    foreign = BUILD / 'test-phase1-bounds-foreign.img'
    shutil.copyfile(seed, foreign)
    payload = (b'FOREIGN-VOLUME-DO-NOT-OVERWRITE-' * (SNAPSHOT // 32 + 1))[:SNAPSHOT]
    assert len(payload) == SNAPSHOT
    with foreign.open('r+b') as handle:
        handle.seek(SNAPSHOT)
        handle.write(payload)
    assert foreign.read_bytes()[SNAPSHOT:2 * SNAPSHOT] == payload
    vm = boot(foreign, args.iso, 'phase1-bounds-foreign-write')
    try:
        vm.wait('MayOS: backup region occupied; legacy single-slot mode')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/foreign-safe.txt saved-without-overwrite', '\nOK\n')
    finally:
        vm.stop()
    assert entries(foreign)['foreign-safe.txt'][3] == 'saved-without-overwrite'
    assert foreign.read_bytes()[SNAPSHOT:2 * SNAPSHOT] == payload, (
        'Foreign backup area was overwritten')
    hash_after_write = file_hash(foreign)
    vm = boot(foreign, args.iso, 'phase1-bounds-foreign-reboot')
    try:
        vm.wait('MayOS: MayFS mounted')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'cat Documents/foreign-safe.txt', '\nsaved-without-overwrite\n')
    finally:
        vm.stop()
    assert file_hash(foreign) == hash_after_write
    print('PASS: valid primary MayFS writes survive while foreign backup bytes remain intact',
          flush=True)

    # Scenario B: an exact 512-sector legacy disk has no room for a backup.
    # Never grow the file or access sector 512; operate in single-slot mode.
    exact = BUILD / 'test-phase1-bounds-512-sectors.img'
    exact.write_bytes(original[:SNAPSHOT])
    assert exact.stat().st_size == SNAPSHOT
    vm = boot(exact, args.iso, 'phase1-bounds-512-write')
    try:
        vm.wait('MayOS: MayFS mounted')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/exact.txt last-valid-sector', '\nOK\n')
    finally:
        vm.stop()
    assert exact.stat().st_size == SNAPSHOT, '512-sector disk was resized'
    assert entries(exact)['exact.txt'][3] == 'last-valid-sector'
    exact_after = file_hash(exact)
    vm = boot(exact, args.iso, 'phase1-bounds-512-reboot')
    try:
        vm.wait('MayOS: MayFS mounted')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'cat Documents/exact.txt', '\nlast-valid-sector\n')
    finally:
        vm.stop()
    assert file_hash(exact) == exact_after
    print('PASS: exact 512-sector legacy disk writes without out-of-range backup I/O',
          flush=True)

    # Scenario C: capacity below the 512-sector minimum. Treat as RAM-only;
    # never attempt any sector write and preserve the complete short disk.
    short = BUILD / 'test-phase1-bounds-511-sectors.img'
    short.write_bytes(original[:SNAPSHOT - SECTOR])
    short_data = short.read_bytes()
    vm = boot(short, args.iso, 'phase1-bounds-511-reject')
    try:
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/ram-only.txt not-on-disk',
                'RAM only; disk sync unavailable')
    finally:
        vm.stop()
    assert short.read_bytes() == short_data, 'Undersized disk unexpectedly modified'
    print('PASS: below-minimum 511-sector disk stays untouched; RAM filesystem fallback',
          flush=True)

    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'iso_sha256': file_hash(args.iso),
        'test': 'MayFS foreign backup region and ATA minimum-sector boundary',
        'foreign_backup_sha256': digest(payload),
        'foreign_backup_unchanged': True,
        'exact_512_sector_disk_persisted': True,
        'exact_512_sector_disk_unchanged_on_read': True,
        'undersized_511_sector_disk_unchanged': True,
        'physical_disk_tested': False,
    }
    (BUILD / 'phase1-storage-bounds-test-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: all MayFS storage boundary cases', flush=True)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Check that MayFS checksum corruption never triggers automatic disk overwrite.

Uses a dedicated throwaway 8 MiB image. This verifies safe refusal to mount,
NOT restoration of the older contents or recovery after power failure.
"""
from __future__ import annotations
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from dual_boot_smoke import BootVM, BUILD, entries


def hash_file(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def boot(disk: Path, tag: str):
    vm = BootVM(iso=(BUILD / 'mayos.iso').resolve(), disk=disk,
                tag=tag, qemu='qemu-system-x86_64', memory=512, timeout=90)
    return vm


def main():
    disk = BUILD / 'test-phase1-corrupt-mayfs.img'
    with disk.open('wb') as stream:
        stream.truncate(8 * 1024 * 1024)
    vm = boot(disk, 'phase1-corrupt-create')
    try:
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        offset = len(vm.text)
        vm.keyboard_command('write Documents/original.txt original-data')
        vm.wait('OK', offset=offset)
    finally:
        vm.stop()
    assert entries(disk)['original.txt'][3] == 'original-data'
    old_valid_hash = hash_file(disk)

    # Deliberately invalidate checksum-covered payload, not the magic header.
    # Do not do this to any real user disk.
    with disk.open('r+b') as stream:
        stream.seek(512 + 128)
        before = stream.read(1)
        assert before
        stream.seek(512 + 128)
        stream.write(bytes([before[0] ^ 0x80]))
    damaged_bytes = disk.read_bytes()
    damaged_hash = hash_file(disk)
    assert damaged_hash != old_valid_hash

    for index in range(2):
        vm = boot(disk, f'phase1-corrupt-refuse-{index + 1}')
        try:
            vm.wait('invalid/unknown disk; preserved, using RAM fallback')
            vm.wait('MayOS: desktop ready')
            vm.key('f1')
            offset = len(vm.text)
            vm.keyboard_command('write Documents/temporary.txt ram-only')
            vm.wait('RAM only; disk sync unavailable', offset=offset)
            offset = len(vm.text)
            vm.keyboard_command('cat Documents/temporary.txt')
            vm.wait('ram-only', offset=offset)
        finally:
            vm.stop()
        assert disk.read_bytes() == damaged_bytes, (
            f'corrupted user data was changed on boot {index + 1}')
        print(f'PASS: corrupt MayFS refused, RAM fallback, disk untouched '
              f'(boot {index + 1})', flush=True)

    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'test': 'corrupt checksum and refuse automatic overwrite',
        'iso_sha256': hash_file(BUILD / 'mayos.iso'),
        'damaged_disk_sha256': damaged_hash,
        'unchanged_after_two_boots': True,
        'automatic_recovery': False,
    }
    (BUILD / 'phase1-corruption-test-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: corrupted MayFS remains untouched across two BIOS boots', flush=True)


if __name__ == '__main__':
    main()

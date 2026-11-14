#!/usr/bin/env python3
"""Verify MayFS dual-snapshot recovery and fail-closed behavior under QEMU.

Damage is injected into COPIES of a dedicated throwaway disk between boots.
This checks deterministic torn-snapshot handling, not real-time power cutting.
Never operate on the user's MayFS image or any physical disk.
"""
from __future__ import annotations
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import struct

from dual_boot_smoke import BootVM, BUILD
from smoke import entries

SLOT_BYTES = 512 * 512
FILE_BYTES = 48 * 4160


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def slot_valid(disk: Path, slot: int) -> bool:
    with disk.open('rb') as stream:
        stream.seek(slot * SLOT_BYTES)
        header = stream.read(512)
        data = stream.read(FILE_BYTES)
    if header[:8] != b'MAYFS001' or len(data) != FILE_BYTES:
        return False
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xffffffff
    return struct.unpack_from('<I', header, 8)[0] == h


def damage_payload(disk: Path, slot: int):
    with disk.open('r+b') as stream:
        offset = slot * SLOT_BYTES + 512 + 128
        stream.seek(offset)
        original = stream.read(1)
        assert original
        stream.seek(offset)
        stream.write(bytes([original[0] ^ 0x80]))


def boot(disk: Path, tag: str) -> BootVM:
    return BootVM(iso=(BUILD / 'mayos.iso').resolve(), disk=disk,
                  tag=tag, qemu='qemu-system-x86_64', memory=512, timeout=90)


def command(vm: BootVM, text: str, output: str):
    offset = len(vm.text)
    vm.keyboard_command(text)
    vm.wait(output, offset=offset, timeout=90)


def main():
    pristine = BUILD / 'test-phase1-pristine-mayfs.img'
    with pristine.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    vm = boot(pristine, 'phase1-recovery-create')
    try:
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/original.txt original-data', '\nOK\n')
    finally:
        vm.stop()
    assert entries(pristine)['original.txt'][3] == 'original-data'
    assert slot_valid(pristine, 0) and slot_valid(pristine, 1), (
        'Both snapshot copies must be committed after a successful save')

    # 1. Corrupt primary payload; backup should mount without modifying disk.
    primary_bad = BUILD / 'test-phase1-primary-damaged.img'
    shutil.copyfile(pristine, primary_bad)
    damage_payload(primary_bad, 0)
    assert not slot_valid(primary_bad, 0) and slot_valid(primary_bad, 1)
    before = digest(primary_bad)
    vm = boot(primary_bad, 'phase1-recovery-from-backup')
    try:
        vm.wait('MayOS: recovered MayFS from backup snapshot')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'cat Documents/original.txt', '\noriginal-data\n')
    finally:
        vm.stop()
    assert digest(primary_bad) == before, 'Recovery mount must not rewrite the disk'
    print('PASS: backup snapshot recovers primary checksum damage, read-only mount', flush=True)

    # 2. A later user save repairs primary before touching surviving backup.
    vm = boot(primary_bad, 'phase1-recovery-repair')
    try:
        vm.wait('MayOS: recovered MayFS from backup snapshot')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/repaired.txt recovered-and-saved', '\nOK\n')
    finally:
        vm.stop()
    assert slot_valid(primary_bad, 0) and slot_valid(primary_bad, 1)
    assert entries(primary_bad)['repaired.txt'][3] == 'recovered-and-saved'
    vm = boot(primary_bad, 'phase1-recovery-persisted')
    try:
        vm.wait('MayOS: MayFS mounted')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'cat Documents/repaired.txt', '\nrecovered-and-saved\n')
    finally:
        vm.stop()
    print('PASS: subsequent save repairs both copies; independent cold boot reads file', flush=True)

    # 3. Simulated interrupted backup write: primary is valid. Never
    # automatically overwrite an unknown / partly written backup region.
    torn_backup = BUILD / 'test-phase1-backup-torn.img'
    shutil.copyfile(pristine, torn_backup)
    with torn_backup.open('r+b') as handle:
        handle.seek(SLOT_BYTES)
        handle.write(b'\0' * 512)
        handle.seek(SLOT_BYTES + 512 + 128)
        handle.write(b'\xaa')
    assert slot_valid(torn_backup, 0) and not slot_valid(torn_backup, 1)
    before_torn = digest(torn_backup)
    vm = boot(torn_backup, 'phase1-recovery-torn-backup')
    try:
        vm.wait('MayOS: MayFS mounted')
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'cat Documents/original.txt', '\noriginal-data\n')
    finally:
        vm.stop()
    assert digest(torn_backup) == before_torn, 'Unknown backup region must stay untouched'
    print('PASS: torn backup does not invalidate or overwrite intact primary', flush=True)

    # 4. Both slots corrupt: never silently format, wipe, or overwrite.
    both_bad = BUILD / 'test-phase1-both-damaged.img'
    shutil.copyfile(pristine, both_bad)
    damage_payload(both_bad, 0)
    damage_payload(both_bad, 1)
    assert not slot_valid(both_bad, 0) and not slot_valid(both_bad, 1)
    damaged_bytes = both_bad.read_bytes()
    for index in range(2):
        vm = boot(both_bad, f'phase1-both-damaged-{index + 1}')
        try:
            vm.wait('invalid/unknown disk; preserved, using RAM fallback')
            vm.wait('MayOS: desktop ready')
            vm.key('f1')
            command(vm, 'write Documents/temporary.txt ram-only',
                    'RAM only; disk sync unavailable')
            command(vm, 'cat Documents/temporary.txt', '\nram-only\n')
        finally:
            vm.stop()
        assert both_bad.read_bytes() == damaged_bytes
    print('PASS: both damaged copies refused; RAM fallback, disk byte-for-byte intact',
          flush=True)

    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'test_type': 'offline torn-snapshot injection, real QEMU boot/readback',
        'iso_sha256': digest(BUILD / 'mayos.iso'),
        'primary_damage_backup_recovered': True,
        'repaired_both_copies_after_save': True,
        'torn_backup_keeps_primary_readonly': True,
        'both_damaged_preserve_bytes': True,
        'physical_power_failure_tested': False,
        'damaged_disk_sha256': digest(both_bad),
    }
    (BUILD / 'phase1-corruption-test-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('All MayFS dual-snapshot corruption tests passed.', flush=True)


if __name__ == '__main__':
    main()

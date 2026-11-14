#!/usr/bin/env python3
"""Hard-kill QEMU at two real MayFS ATA write checkpoints, then cold-boot normal ISO.

Diagnostic-only kernel hangs intentionally at a specified checkpoint; the host
sends SIGKILL (not ACPI shutdown, reboot, or a clean QMP quit). The original
normal ISO is used to check recovery on a PRIVATE 8 MiB test disk. This is
emulated crash consistency testing, not a physical power failure guarantee.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import struct
from dual_boot_smoke import BootVM, BUILD
from smoke import entries

SLOT_BYTES = 512 * 512
FILE_COUNT = 48
FILE_STRUCT_SIZE = 4160


def sha(path: Path) -> str:
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def snapshot(disk: Path, slot: int):
    with disk.open('rb') as stream:
        stream.seek(slot * SLOT_BYTES)
        image = stream.read(SLOT_BYTES)
    assert len(image) == SLOT_BYTES
    if image[:8] != b'MAYFS001':
        return {'valid': False, 'generation': None, 'files': {}}
    payload = image[512:512 + FILE_COUNT * FILE_STRUCT_SIZE]
    h = 2166136261
    for value in payload:
        h = ((h ^ value) * 16777619) & 0xffffffff
    valid = struct.unpack_from('<I', image, 8)[0] == h
    if not valid:
        return {'valid': False, 'generation': None, 'files': {}}
    gen = 0
    if image[12:16] == b'MAY2':
        number, complement = struct.unpack_from('<II', image, 16)
        if complement == (number ^ 0xffffffff):
            gen = number
    files = {}
    for index in range(FILE_COUNT):
        at = 512 + index * FILE_STRUCT_SIZE
        used, kind, parent, size = struct.unpack_from('<4I', image, at)
        if used and not kind:
            assert size < 4096
            name = image[at + 16:at + 64].split(b'\0')[0].decode('ascii')
            content = image[at + 64:at + 64 + size].decode('ascii')
            files[name] = content
    return {'valid': True, 'generation': gen, 'files': files}


def new_vm(disk: Path, iso: Path, tag: str) -> BootVM:
    return BootVM(iso=iso.resolve(), disk=disk, tag=tag,
                  qemu='qemu-system-x86_64', memory=512, timeout=90)


def command(vm: BootVM, line: str, response: str):
    offset = len(vm.text)
    vm.keyboard_command(line)
    vm.wait(response, offset=offset)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--normal-iso', type=Path, required=True)
    parser.add_argument('--diagnostic-iso', type=Path, required=True)
    args = parser.parse_args()
    for iso in (args.normal_iso, args.diagnostic_iso):
        if not iso.is_file():
            parser.error(f'ISO not found: {iso}')
    normal_sha = sha(args.normal_iso)
    diagnostic_sha = sha(args.diagnostic_iso)
    assert normal_sha != diagnostic_sha, (
        'Diagnostic build must be independent of the normal ISO')
    assert b'FS POWERLOSS DIAGNOSTIC' not in args.normal_iso.read_bytes(), (
        'Normal ISO must NOT contain the diagnostic halt hook')
    assert b'FS POWERLOSS DIAGNOSTIC' in args.diagnostic_iso.read_bytes(), (
        'Diagnostic ISO does not include the expected halt hook')

    pristine = BUILD / 'test-phase1-powercut-pristine.img'
    with pristine.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    vm = new_vm(pristine, args.normal_iso, 'phase1-powercut-seed')
    try:
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/base.txt previous-save-survived', '\nOK\n')
    finally:
        vm.stop()
    primary = snapshot(pristine, 0)
    backup = snapshot(pristine, 1)
    assert primary['valid'] and backup['valid'], 'Seed should have two valid snapshots'
    assert primary['files']['base.txt'] == 'previous-save-survived'
    assert backup['files']['base.txt'] == 'previous-save-survived'
    assert primary['generation'] == backup['generation']

    cases = (
        ('before', 'cut-before.txt', 'partial-backup',
         'data-that-must-not-appear', False),
        ('after', 'cut-after.txt', 'backup-committed',
         'data-that-must-survive', True),
    )
    samples = []
    for kind, filename, checkpoint, value, should_survive in cases:
        disk = BUILD / f'test-phase1-powercut-{kind}.img'
        shutil.copyfile(pristine, disk)
        before = sha(disk)
        assert snapshot(disk, 0)['valid'] and snapshot(disk, 1)['valid']
        diag = new_vm(disk, args.diagnostic_iso, f'phase1-powercut-{kind}-inject')
        guest_killed = False
        try:
            diag.wait('MayOS: MayFS mounted')
            diag.wait('MayOS: desktop ready')
            diag.key('f1')
            diag.keyboard_command(f'write Documents/{filename} {value}')
            diag.wait(f'MayOS: FS POWERLOSS DIAGNOSTIC checkpoint={checkpoint}')
            assert diag.proc.poll() is None, 'QEMU exited before deliberate hard kill'
            diag.proc.kill()  # SIGKILL: no graceful device shutdown
            diag.proc.wait(timeout=5)
            guest_killed = True
        finally:
            diag.stop()
        assert guest_killed, 'Guest was not stopped at intended checkpoint'
        after_kill = sha(disk)
        main_copy = snapshot(disk, 0)
        backup_copy = snapshot(disk, 1)
        assert main_copy['valid'], 'Last committed primary was lost during crash'
        assert main_copy['files']['base.txt'] == 'previous-save-survived'
        if should_survive:
            assert backup_copy['valid'], 'Committed backup missing after hard kill'
            assert backup_copy['generation'] > main_copy['generation'], (
                'Committed backup does not outrank old primary')
            assert backup_copy['files'][filename] == value
        else:
            assert not backup_copy['valid'] or (
                backup_copy['generation'] == main_copy['generation']), (
                'Uncommitted partial backup appears newer than stable primary')
        vm = new_vm(disk, args.normal_iso, f'phase1-powercut-{kind}-reboot')
        try:
            vm.wait('MayOS: MayFS mounted')
            if should_survive:
                vm.wait('MayOS: recovered MayFS from backup snapshot')
            vm.wait('MayOS: desktop ready')
            vm.key('f1')
            command(vm, 'cat Documents/base.txt', '\nprevious-save-survived\n')
            offset = len(vm.text)
            vm.keyboard_command(f'cat Documents/{filename}')
            if should_survive:
                vm.wait('\n' + value + '\n', offset=offset)
            else:
                vm.wait('cat: file not found', offset=offset)
        finally:
            vm.stop()
        assert sha(disk) == after_kill, (
            f'Cold read-only recovery unexpectedly wrote to the {kind} disk')
        if not should_survive:
            assert entries(disk)['base.txt'][3] == 'previous-save-survived'
        samples.append({
            'checkpoint': checkpoint,
            'sigkill': True,
            'last_committed_base_survived': True,
            'new_write_committed': should_survive,
            'primary_valid': main_copy['valid'],
            'backup_valid': backup_copy['valid'],
            'disk_before_sha256': before,
            'disk_after_sigkill_sha256': after_kill,
            'disk_after_coldboot_sha256': sha(disk),
        })
        print(f'PASS: SIGKILL at {checkpoint}, normal ISO reboot recovers '
              f'{"new committed data" if should_survive else "prior data"} '
              'with no recovery write', flush=True)

    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'test': 'QEMU SIGKILL at real ATA write checkpoints, then normal-ISO cold boot',
        'normal_iso_sha256': normal_sha,
        'diagnostic_iso_sha256': diagnostic_sha,
        'normal_iso_contains_diagnostic': False,
        'physical_power_failure_tested': False,
        'test_disk': 'private 8MiB copies only',
        'cases': samples,
    }
    (BUILD / 'phase1-powercut-test-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: diagnostic-only hard-power-cut regressions', flush=True)


if __name__ == '__main__':
    main()

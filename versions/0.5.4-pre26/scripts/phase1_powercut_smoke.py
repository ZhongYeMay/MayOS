#!/usr/bin/env python3
"""Hard-kill QEMU at MayFS write checkpoints and verify cold recovery.

The diagnostic kernel halts at a checkpoint, and the host sends SIGKILL.
Only disposable disk images are used. --4kn uses an installed GPT/UEFI
NVMe disk and cold-boots the installed kernel without an ISO.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from dual_boot_smoke import BootVM, BUILD
from install_mayos import SECTOR_4K, install
from installed_storage_smoke import complete_setup, gpt_layout

SLOT_BYTES = 512 * 512
FILE_COUNT = 48
FILE_STRUCT_SIZE = 4160


def sha(path: Path) -> str:
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def snapshot(disk: Path, slot: int, start: int, expanded: bool):
    slot_bytes = 3584 * 1024 if expanded else SLOT_BYTES
    offset = (512 * 1024, 4 * 1024 * 1024)[slot] if expanded else slot * SLOT_BYTES
    file_count = 96 if expanded else FILE_COUNT
    record_bytes = 32832 if expanded else FILE_STRUCT_SIZE
    magic = b'MAYFS002' if expanded else b'MAYFS001'
    with disk.open('rb') as stream:
        stream.seek(start + offset)
        image = stream.read(slot_bytes)
    assert len(image) == slot_bytes
    if image[:8] != magic:
        return {'valid': False, 'generation': None, 'files': {}}
    payload = image[512:512 + file_count * record_bytes]
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
    for index in range(file_count):
        at = 512 + index * record_bytes
        used, kind, parent, size = struct.unpack_from('<4I', image, at)
        if used and not kind:
            assert size < record_bytes - 64
            name = image[at + 16:at + 64].split(b'\0')[0].decode('ascii')
            content = image[at + 64:at + 64 + size].decode('ascii')
            files[name] = content
    return {'valid': True, 'generation': gen, 'files': files}


def nvme_vm(root, disk, iso, tag, code, variables):
    qemu = shutil.which("qemu-system-x86_64")
    vars_path = root / f"{tag}.fd"
    shutil.copyfile(variables, vars_path)
    wrapper = root / f"{tag}-qemu"
    wrapper.write_text(f"""#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
i = args.index('-drive')
args[i + 1] = 'file={disk},format=raw,if=none,id=nvme-test,cache=directsync'
args += ['-device', 'nvme,drive=nvme-test,serial=MAYOS-CUT,logical_block_size=4096,physical_block_size=4096,bootindex=1']
if '-cdrom' in args:
    i = args.index('-cdrom')
    args[i:i+2] = ['-drive', 'file=' + args[i + 1] + ',if=none,media=cdrom,id=diag',
                   '-device', 'ide-cd,bus=ide.0,drive=diag,bootindex=0']
os.execv({qemu!r}, [{qemu!r}, *args])
""")
    wrapper.chmod(0o755)
    return BootVM(iso=iso, disk=disk, tag=tag, qemu=str(wrapper),
                  memory=512, timeout=90, machine="q35", ovmf_code=code, ovmf_vars=vars_path)


def command(vm: BootVM, line: str, response: str):
    offset = len(vm.text)
    vm.keyboard_command(line)
    vm.wait(response, offset=offset)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--normal-iso', type=Path, required=True)
    parser.add_argument('--diagnostic-iso', type=Path, required=True)
    parser.add_argument('--4kn', dest='native_4k', action='store_true')
    parser.add_argument('--ovmf-code', type=Path)
    parser.add_argument('--ovmf-vars', type=Path)
    args = parser.parse_args()
    if args.native_4k and not (args.ovmf_code and args.ovmf_vars):
        parser.error('--4kn requires --ovmf-code and --ovmf-vars')
    run(args)


def run(args):
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

    with tempfile.TemporaryDirectory(prefix="mayos-powercut-") as directory:
        root = Path(directory)
        test_disks(args, root, normal_sha, diagnostic_sha)


def test_disks(args, root, normal_sha, diagnostic_sha):
    pristine = root / 'pristine.img'
    start = 0
    if args.native_4k:
        install(args.normal_iso, normal_sha, pristine, sector=SECTOR_4K)
        layout = gpt_layout(pristine, SECTOR_4K, verify=True)
        start = layout["partitions"][1]["start"] * SECTOR_4K

        def boot(disk, tag, diagnostic=False):
            return nvme_vm(root, disk, args.diagnostic_iso.resolve() if diagnostic else None,
                           f"cut4-{tag}", args.ovmf_code, args.ovmf_vars)
    else:
        with pristine.open('xb') as handle:
            handle.truncate(8 * 1024 * 1024)

        def boot(disk, tag, diagnostic=False):
            iso = args.diagnostic_iso if diagnostic else args.normal_iso
            return BootVM(iso=iso.resolve(), disk=disk, tag=f"cut512-{tag}",
                          qemu=shutil.which("qemu-system-x86_64"), memory=512, timeout=90)

    vm = boot(pristine, 'seed')
    try:
        if args.native_4k:
            vm.wait('MayOS: installed disk matched on NVMe')
            vm.wait('MayOS: first boot setup ready')
            complete_setup(vm)
        vm.wait('MayOS: desktop ready')
        vm.key('f1')
        command(vm, 'write Documents/base.txt previous-save-survived', '\nOK\n')
    finally:
        vm.stop()
    primary = snapshot(pristine, 0, start, args.native_4k)
    backup = snapshot(pristine, 1, start, args.native_4k)
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
        disk = root / f'powercut-{kind}.img'
        subprocess.run(['cp', '--sparse=always', str(pristine), str(disk)], check=True)
        before = sha(disk)
        assert snapshot(disk, 0, start, args.native_4k)['valid'] and snapshot(disk, 1, start, args.native_4k)['valid']
        diag = boot(disk, f'{kind}-inject', diagnostic=True)
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
        main_copy = snapshot(disk, 0, start, args.native_4k)
        backup_copy = snapshot(disk, 1, start, args.native_4k)
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
        vm = boot(disk, f'{kind}-reboot')
        try:
            if args.native_4k:
                vm.wait('MayOS: installed disk matched on NVMe')
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
        if args.native_4k:
            vm = boot(disk, f'{kind}-save')
            try:
                vm.wait('MayOS: desktop ready')
                vm.key('f1')
                command(vm, 'write Documents/restored.txt restored-redundancy', '\nOK\n')
            finally:
                vm.stop()
            for slot in (0, 1):
                restored = snapshot(disk, slot, start, args.native_4k)
                assert restored['valid'] and restored['files']['restored.txt'] == 'restored-redundancy'
            print(f'PASS: normal save after {checkpoint} restored both 4Kn MayFS snapshots')
        samples.append({
            'checkpoint': checkpoint,
            'sigkill': True,
            'last_committed_base_survived': True,
            'new_write_committed': should_survive,
            'primary_valid': main_copy['valid'],
            'backup_valid': backup_copy['valid'],
            'disk_before_sha256': before,
            'disk_after_sigkill_sha256': after_kill,
            'disk_after_coldboot_sha256': after_kill,
        })
        source = 'installed ISO-free cold boot' if args.native_4k else 'normal ISO reboot'
        print(f'PASS: SIGKILL at {checkpoint}, {source} recovers '
              f'{"new committed data" if should_survive else "prior data"} '
              'with no recovery write', flush=True)

    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'test': 'QEMU SIGKILL at MayFS write checkpoints, then cold recovery',
        'normal_iso_sha256': normal_sha,
        'diagnostic_iso_sha256': diagnostic_sha,
        'normal_iso_contains_diagnostic': False,
        'physical_power_failure_tested': False,
        'test_disk': 'private GPT/UEFI NVMe 4Kn copies' if args.native_4k else 'private 8 MiB ATA copies',
        'cases': samples,
    }
    name = '4kn-powercut-test-results.json' if args.native_4k else 'phase1-powercut-test-results.json'
    (BUILD / name).write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: diagnostic-only hard-power-cut regressions', flush=True)


if __name__ == '__main__':
    main()

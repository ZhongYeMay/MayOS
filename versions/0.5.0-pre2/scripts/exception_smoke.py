#!/usr/bin/env python3
"""Boot a deliberately faulting DIAGNOSTIC ISO and verify serial exception output.

The normal release ISO must never have EXCEPTION_SELFTEST=1. This test has a
separate QEMU invocation and no access to the user's persistent data disk.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

BUILD = Path(__file__).resolve().parents[1] / 'build'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=BUILD / 'mayos.iso')
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--timeout', type=float, default=45)
    args = parser.parse_args()
    if not args.iso.is_file():
        parser.error(f'missing self-test ISO: {args.iso}')
    serial = BUILD / 'exception-selftest.log'
    stderr = BUILD / 'exception-selftest-qemu.log'
    serial.unlink(missing_ok=True)
    cmd = [args.qemu, '-machine', 'pc', '-accel', 'tcg',
           '-cpu', 'max', '-m', '512', '-vga', 'std', '-display', 'none',
           '-nic', 'none', '-cdrom', str(args.iso.resolve()), '-boot', 'd',
           '-serial', f'file:{serial}', '-no-reboot', '-no-shutdown']
    with stderr.open('w') as error_log:
        guest = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=error_log)
        try:
            deadline = time.monotonic() + args.timeout
            while True:
                lines = serial.read_text(errors='replace') if serial.exists() else ''
                if 'MayOS: FATAL EXCEPTION' in lines:
                    assert 'MayOS: intentional invalid-opcode exception self-test' in lines, (
                        'Fault may be unrelated to intentional diagnostic test')
                    assert re.search(
                        r'MayOS: FATAL EXCEPTION vector=6 error=0x[0-9a-f]{16} '
                        r'rip=0x[0-9a-f]{16}', lines
                    ), f'Unexpected exception log:\n{lines}'
                    assert 'MayOS: desktop ready' not in lines, (
                        'Diagnostic build must fault before the desktop is ready')
                    break
                if guest.poll() is not None:
                    raise RuntimeError('QEMU stopped before expected exception: ' + lines[-1500:])
                if time.monotonic() >= deadline:
                    raise TimeoutError('No x86_64 exception diagnostic in serial output: ' +
                                       lines[-1500:])
                time.sleep(.05)
        finally:
            if guest.poll() is None:
                guest.terminate()
            try:
                guest.wait(timeout=5)
            except subprocess.TimeoutExpired:
                guest.kill()
                guest.wait(timeout=5)
    with args.iso.open('rb') as handle:
        iso_hash = hashlib.file_digest(handle, 'sha256').hexdigest()
    report = {
        'tested_at_utc': datetime.now(timezone.utc).isoformat(),
        'test_type': 'intentionally faulting diagnostic ISO, NOT a release build',
        'test': 'x86_64 invalid opcode (#UD), vector=6',
        'iso_sha256': iso_hash,
        'serial_log': serial.name,
        'qemu': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0]
    }
    (BUILD / 'phase1-exception-test-results.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: controlled #UD exception was recorded with vector/error/RIP', flush=True)


if __name__ == '__main__':
    main()

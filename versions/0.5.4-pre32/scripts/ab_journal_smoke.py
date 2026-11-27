#!/usr/bin/env python3
"""Actual x86_64 MayKernel QEMU ATA durable journal diagnostic.

Test-only diagnostic binary; NO production install, GRUB boot-slot change,
or real signed artifact staged. The test explicitly constructs isolated 8MiB
MayFS disk images with an opt-in marker and writes ONLY LBAs 1024/1025.
"""
from pathlib import Path
import struct

from smoke import VM, BUILD
from rust_smoke import MANIFEST, FS_COUNT, RECORD, snapshot

MARKER = b"ISOLATED-QEMU-AB-JOURNAL-ONLY"
LBA_A, LBA_B = 1024, 1025
SECTOR = 512
MAYFS_BYTES = 1024 * SECTOR

def marked_image(path: Path, marker: bool = True, foreign: bool = False):
    image = snapshot(MANIFEST)
    if marker:
        records = bytearray(image[512:512 + FS_COUNT*RECORD])
        pos = 5*RECORD
        struct.pack_into("<4I", records, pos, 1, 0, 1, len(MARKER))
        records[pos+16:pos+16+len(b"AB-DIAGNOSTIC.marker")] = b"AB-DIAGNOSTIC.marker"
        records[pos+64:pos+64+len(MARKER)] = MARKER
        checksum = 2166136261
        for ch in records:
            checksum = ((checksum^ch)*16777619)&0xffffffff
        image[512:512+len(records)]=records
        struct.pack_into("<I",image,8,checksum)
    with path.open("wb") as disk:
        disk.truncate(8*1024*1024)
        disk.seek(0);disk.write(image)
        disk.seek(512*SECTOR);disk.write(image)
        if foreign:
            disk.seek(LBA_A*SECTOR);disk.write(b"FOREIGN-RESERVED-DATA")
    return path

def run(path: Path, name: str, command: str, expect: str, capture=True):
    vm=VM(path, name)
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd(command)
        vm.wait(expect)
        if capture:vm.shot(name)
    finally:
        vm.stop()

def read_sector(path, lba):
    with path.open("rb") as disk:
        disk.seek(lba*SECTOR)
        return disk.read(SECTOR)

def main():
    disk=marked_image(BUILD/"ab-journal-8mib-test.img")
    before=disk.read_bytes()[:MAYFS_BYTES]
    assert read_sector(disk,LBA_A) == bytes(SECTOR)
    assert read_sector(disk,LBA_B) == bytes(SECTOR)

    run(disk,"ab-journal-init","update journal-init",
        "MayOS: AB journal initialized and ATA FLUSH completed")
    assert read_sector(disk,LBA_A)[:8] == b"MAYABJ01"
    assert read_sector(disk,LBA_B) == bytes(SECTOR)
    run(disk,"ab-journal-cold-start","update journal-status",
        "MayOS: AB journal recovered generation=1 active=0 trial=2")
    # The synthetic trial is diagnostic-only: NO signed release is staged.
    run(disk,"ab-journal-stage","update journal-stage",
        "MayOS: AB journal synthetic trial B committed and ATA FLUSH completed")
    assert read_sector(disk,LBA_B)[:8] == b"MAYABJ01"
    run(disk,"ab-journal-recover-newest","update journal-status",
        "MayOS: AB journal recovered generation=2 active=0 trial=1")
    # Stop after flush: recover new generation on true QEMU cold boot.
    # Corrupt the latest record between boots, simulating a torn/damaged
    # journal sector, not claiming physical power-cut modeling.
    with disk.open("r+b") as f:
        f.seek(LBA_B*SECTOR+16+40)  # Rust record FNV checksum
        original=f.read(1)
        assert len(original)==1
        f.seek(LBA_B*SECTOR+16+40)
        f.write(bytes([original[0]^0x40]))
    run(disk,"ab-journal-recover-old","update journal-status",
        "MayOS: AB journal recovered generation=1 active=0 trial=2")
    assert disk.read_bytes()[:MAYFS_BYTES] == before, "MayFS snapshots overwritten"

    # A non-MAYOS sector MUST be preserved even on correctly marked 8MiB disk.
    foreign=marked_image(BUILD/"ab-journal-foreign.img",foreign=True)
    original=foreign.read_bytes()
    run(foreign,"ab-journal-foreign-refused","update journal-init",
        "MayOS: AB disk diagnostic REFUSED (reserved sectors contain foreign data)",
        capture=False)
    assert foreign.read_bytes()==original, "Foreign disk bytes were changed"
    # Absence of explicit marker MUST refuse writes even if disk otherwise fits.
    no_marker=marked_image(BUILD/"ab-journal-unmarked.img",marker=False)
    original=no_marker.read_bytes()
    run(no_marker,"ab-journal-unmarked-refused","update journal-init",
        "MayOS: AB disk diagnostic REFUSED (not an isolated marked 8MiB test disk)",
        capture=False)
    assert no_marker.read_bytes()==original, "Unmarked disk bytes were changed"
    print("PASS: actual QEMU ATA journal persists across cold boots; newer mirror selected",flush=True)
    print("PASS: damaged newest mirror falls back to older valid record; MayFS 0..1023 untouched",flush=True)
    print("PASS: foreign/unmarked disks unchanged; synthetic journal only, NO INSTALL",flush=True)

if __name__=="__main__":
    main()

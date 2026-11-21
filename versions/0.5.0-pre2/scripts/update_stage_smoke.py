#!/usr/bin/env python3
"""Real native MayKernel/QEMU streaming image staging diagnostic.

64 KiB is >16x MayFS file cap and uses actual ATA PIO disk writes, flushes
and dual progress headers. Does NOT use HTTPS, signed Release or installation.
The 8MiB QEMU disk has a strict marker; unrecognized disks must be preserved.
"""
from pathlib import Path
import shutil
import struct
from smoke import VM, BUILD
from rust_smoke import MANIFEST, snapshot, FS_COUNT, RECORD

MARKER = b"ISOLATED-QEMU-UPDATE-STAGE-ONLY"
SECTOR = 512
MAYFS_END = 1024*SECTOR
HEADER_A = 1536
HEADER_B = 1537
DATA_LBA = 2048
FIXTURE_BYTES = 65536


def marked_disk(path, enabled=True, foreign=False):
    img=snapshot(MANIFEST)
    if enabled:
        records=bytearray(img[512:512+FS_COUNT*RECORD])
        pos=5*RECORD
        struct.pack_into("<4I",records,pos,1,0,1,len(MARKER))
        label=b"STAGE-DIAGNOSTIC.marker"
        records[pos+16:pos+16+len(label)]=label
        records[pos+64:pos+64+len(MARKER)]=MARKER
        value=2166136261
        for b in records:
            value=((value^b)*16777619)&0xffffffff
        img[512:512+len(records)]=records
        struct.pack_into("<I",img,8,value)
    with path.open("wb") as f:
        f.truncate(8*1024*1024)
        f.seek(0);f.write(img)
        f.seek(512*SECTOR);f.write(img)
        if foreign:
            f.seek((DATA_LBA+19)*SECTOR)
            f.write(b"FOREIGN-USER-DATA")
    return path


def sector(path,lba):
    with path.open("rb") as f:
        f.seek(lba*SECTOR)
        return f.read(SECTOR)


def boot_command(path,tag,cmd,message):
    vm=VM(path,tag)
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd(cmd)
        vm.wait(message)
        vm.shot(tag)
    finally:
        vm.stop()


def pattern(n):
    return bytes((((offset*13)^(offset>>5)^0xa5)&255) for offset in range(n))


def main():
    disk=marked_disk(BUILD/"stage-large-test.img")
    preserved=disk.read_bytes()[:MAYFS_END]
    assert sector(disk,HEADER_A)==bytes(SECTOR)
    assert sector(disk,HEADER_B)==bytes(SECTOR)
    boot_command(disk,"stage-setup","update stage-begin",
                 "MayOS: staging header initialized; ATA FLUSH confirmed; NO INSTALL")
    assert sector(disk,HEADER_A)[:8]==b"MAYSTG01"
    boot_command(disk,"stage-first-half","update stage-half",
                 "MayOS: staging recovered bytes=32768/65536")
    assert sector(disk,HEADER_B)[:8]==b"MAYSTG01"
    boot_command(disk,"stage-half-reboot","update stage-status",
                 "MayOS: staging recovered bytes=32768/65536")
    boot_command(disk,"stage-resume","update stage-rest",
                 "MayOS: staging recovered bytes=65536/65536")
    assert disk.read_bytes()[:MAYFS_END]==preserved,"MayFS snapshots were overwritten"
    with disk.open("rb") as f:
        f.seek(DATA_LBA*SECTOR)
        actual=f.read(FIXTURE_BYTES)
    assert actual==pattern(FIXTURE_BYTES),"ATA payload differs from streamed fixture"

    bad=BUILD/"stage-corrupted-test.img"
    shutil.copyfile(disk,bad)
    with bad.open("r+b") as f:
        f.seek(DATA_LBA*SECTOR+16385)
        original=f.read(1)
        assert len(original)==1
        f.seek(DATA_LBA*SECTOR+16385)
        f.write(bytes([original[0]^0x40]))
    boot_command(bad,"stage-hash-rejected","update stage-verify",
                 "MayOS: staging SHA-256 MISMATCH (NO INSTALL)")
    boot_command(disk,"stage-sha-verified","update stage-verify",
                 "MayOS: staged image SHA-256 verified from ATA disk; NO INSTALL")
    boot_command(disk,"stage-verified-reboot","update stage-status",
                 "SHA-256=VALID (NOT SIGNED; NO INSTALL)")
    assert disk.read_bytes()[:MAYFS_END]==preserved
    assert bad.read_bytes()[:MAYFS_END]==preserved

    # Malicious or foreign information in the reserved staging data region
    # must never be zeroed by the diagnostic's initialisation step.
    foreign=marked_disk(BUILD/"stage-foreign-test.img",foreign=True)
    original=foreign.read_bytes()
    boot_command(foreign,"stage-foreign-rejected","update stage-begin",
                 "MayOS: staging begin REFUSED (reserved sectors must be blank)")
    assert foreign.read_bytes()==original,"Unknown user's data was modified"

    unmarked=marked_disk(BUILD/"stage-unmarked-test.img",enabled=False)
    original=unmarked.read_bytes()
    boot_command(unmarked,"stage-unmarked-rejected","update stage-begin",
                 "MayOS: staging REFUSED (not a marked isolated 8MiB QEMU disk)")
    assert unmarked.read_bytes()==original,"Unmarked disk was modified"
    print("PASS: actual QEMU ATA 64KiB streamed image survives multiple cold boots",flush=True)
    print("PASS: signed-install DISABLED; streaming SHA-256 rejects modified image bytes",flush=True)
    print("PASS: MayFS preserved, foreign/unmarked disks unchanged; QEMU fixture only",flush=True)

if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""Real Rust+C MayKernel end-to-end authenticated 64KiB staged image test.

CI creates an ephemeral Ed25519 key, signs a real 64KiB deterministic payload
offline with scripts/package_signed_release.py and pins the corresponding
public key into native x86_64 MayKernel. The QEMU test proves accepted and
tampered-signature/image rejection on the actual ATA data bytes.

This is a test-only QEMU stage, NOT a GitHub download or bootable ISO install.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
from rust_smoke import snapshot, FS_COUNT, RECORD
from smoke import VM, BUILD

SECTOR=512
FS_COPY_SECTORS=512
STAGED_DATA_LBA=2048
SAMPLE_BYTES=65536
MARKER=b"ISOLATED-QEMU-UPDATE-STAGE-ONLY"


def payload_bytes() -> bytes:
    return bytes((((i*13)^(i>>5)^0xa5)&255) for i in range(SAMPLE_BYTES))


def make_fs_copy(manifest: bytes, signature: bytes):
    if not (0<len(manifest)<=2048 and len(signature)==64):
        raise ValueError("signed test manifest must fit MayFS")
    img=snapshot(manifest)
    records=bytearray(img[512:512+FS_COUNT*RECORD])
    for slot,name,data in [
        (5,b"STAGE-DIAGNOSTIC.marker",MARKER),
        (6,b"Update.sig",signature),
    ]:
        pos=slot*RECORD
        assert len(name)<48 and len(data)<4096
        struct.pack_into("<4I",records,pos,1,0,1,len(data))
        records[pos+16:pos+16+len(name)]=name
        records[pos+64:pos+64+len(data)]=data
    checksum=2166136261
    for byte in records:
        checksum=((checksum^byte)*16777619)&0xffffffff
    img[512:512+len(records)]=records
    struct.pack_into("<I",img,8,checksum)
    return img


def make_disk(path: Path,manifest: bytes, signature: bytes):
    img=make_fs_copy(manifest,signature)
    with path.open("wb") as f:
        f.truncate(8*1024*1024)
        f.seek(0);f.write(img)
        f.seek(FS_COPY_SECTORS*SECTOR);f.write(img)
    return path


def change_signature(path: Path, signature: bytes):
    if len(signature)!=64:raise ValueError("bad signature size")
    with path.open("r+b") as f:
        for copy in range(2):
            base=copy*FS_COPY_SECTORS*SECTOR
            f.seek(base);snapshot_data=bytearray(f.read(FS_COPY_SECTORS*SECTOR))
            position=512+6*RECORD+64
            snapshot_data[position:position+64]=signature
            records=snapshot_data[512:512+FS_COUNT*RECORD]
            checksum=2166136261
            for value in records:
                checksum=((checksum^value)*16777619)&0xffffffff
            struct.pack_into("<I",snapshot_data,8,checksum)
            f.seek(base);f.write(snapshot_data)


def guest(path: Path,name: str,commands: list[str],expected: str):
    vm=VM(path,name)
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        for command in commands:
            vm.cmd(command)
        vm.wait(expected)
        vm.shot(name)
    finally:
        vm.stop()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--package-dir",type=Path,required=True)
    parser.add_argument("--image",type=Path,required=True)
    a=parser.parse_args()
    raw_manifest=(a.package_dir/"Update.may").read_bytes()
    sig=(a.package_dir/"Update.sig").read_bytes()
    assert a.image.read_bytes()==payload_bytes(),"offline signed payload does not match native test pattern"
    assert hashlib.sha256(a.image.read_bytes()).hexdigest().encode() in raw_manifest
    assert b"size_bytes=65536\n" in raw_manifest

    disk=make_disk(BUILD/"signed-staged-image-valid.img",raw_manifest,sig)
    fs_before=disk.read_bytes()[:2*FS_COPY_SECTORS*SECTOR]
    guest(disk,"signed-stage-start",["update stage-begin","update stage-half"],
          "MayOS: staging recovered bytes=32768/65536")
    incomplete=BUILD/"signed-staged-image-incomplete.img"
    shutil.copyfile(disk,incomplete)
    guest(incomplete,"signed-stage-incomplete-rejected",["update auth-stage"],
          "Signed staging: signed digest differs from ATA staged payload")
    guest(disk,"signed-stage-resume",["update stage-rest","update stage-verify"],
          "MayOS: staged image SHA-256 verified from ATA disk; NO INSTALL")
    guest(disk,"signed-stage-authenticated",["update auth-stage"],
          "MayOS: signed manifest + staged ATA data authenticated; NO INSTALL")
    assert disk.read_bytes()[:2*FS_COPY_SECTORS*SECTOR]==fs_before

    altered_sig=BUILD/"signed-staged-image-bad-sig.img"
    shutil.copyfile(disk,altered_sig)
    broken=bytearray(sig);broken[0]^=0x40
    change_signature(altered_sig,bytes(broken))
    guest(altered_sig,"signed-stage-signature-rejected",["update auth-stage"],
          "Signed staging: publisher Ed25519 signature REJECTED")

    altered_image=BUILD/"signed-staged-image-bad-data.img"
    shutil.copyfile(disk,altered_image)
    with altered_image.open("r+b") as f:
        offset=STAGED_DATA_LBA*SECTOR+16385
        f.seek(offset);old=f.read(1)
        f.seek(offset);f.write(bytes([old[0]^0x20]))
    guest(altered_image,"signed-stage-ata-tamper-rejected",["update auth-stage"],
          "Signed staging: signed digest differs from ATA staged payload")
    assert altered_image.read_bytes()[:2*FS_COPY_SECTORS*SECTOR]==fs_before
    print("PASS: offline owner-key Ed25519 64KiB signed metadata -> native QEMU ATA image verified",flush=True)
    print("PASS: QEMU rejects incomplete stage, modified Ed25519 signature and post-verify ATA tampering",flush=True)
    print("PASS: MayFS primary/backup snapshots remained unmodified; NO INSTALL",flush=True)


if __name__=="__main__":
    main()

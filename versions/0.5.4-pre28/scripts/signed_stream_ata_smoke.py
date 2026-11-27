#!/usr/bin/env python3
"""Real QEMU Stage 6J: signed fixture -> native Rust HTTP budget -> ATA.

Only an ephemeral CI Ed25519 key and explicitly marked 8 MiB isolated test
disk are used. These are OFFLINE deterministic payloads, not HTTPS downloads,
not system installation, not a bootloader or A/B partition update.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
from signed_stage_e2e_smoke import make_disk, payload_bytes, STAGED_DATA_LBA
from smoke import VM, BUILD

SECTOR=512
MAYFS_END=1024*SECTOR
BODY_BYTES=65536
STAGE_OFFSET=STAGED_DATA_LBA*SECTOR

def guest(disk:Path,name:str,command:str,message:str):
    vm=VM(disk,name)
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd(command)
        vm.wait(message)
        vm.shot(name)
    finally:
        vm.stop()

def assert_stage_blank(disk:Path):
    with disk.open("rb") as f:
        f.seek(1536*SECTOR)
        assert f.read(2*SECTOR)==bytes(2*SECTOR), "unexpected staging header writes"
        f.seek(STAGE_OFFSET)
        assert f.read(BODY_BYTES)==bytes(BODY_BYTES), "unexpected staging body writes"

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--package-dir",required=True,type=Path)
    parser.add_argument("--wrong-hash-package-dir",required=True,type=Path)
    args=parser.parse_args()
    valid=(args.package_dir/"Update.may").read_bytes()
    signature=(args.package_dir/"Update.sig").read_bytes()
    wrong=(args.wrong_hash_package_dir/"Update.may").read_bytes()
    wrong_sig=(args.wrong_hash_package_dir/"Update.sig").read_bytes()
    assert len(signature)==len(wrong_sig)==64
    assert b"size_bytes=65536\n" in valid and b"size_bytes=65536\n" in wrong
    assert valid!=wrong

    good_disk=make_disk(BUILD/"signed-ata-valid.img",valid,signature)
    good_before=good_disk.read_bytes()[:MAYFS_END]
    guest(good_disk,"signed-ata-valid","update auth-stream-stage-test",
          "MayOS: signed 64KiB stream staged to isolated ATA and rehashed PASS; NO INSTALL")
    assert good_disk.read_bytes()[:MAYFS_END]==good_before
    with good_disk.open("rb") as f:
        f.seek(STAGE_OFFSET)
        assert f.read(BODY_BYTES)==payload_bytes(), "staged ATA bytes differ from signed fixture"
    print("PASS: signed 64KiB HTTP-like stream committed, ATA readback and Ed25519 verified; no installation",flush=True)

    guest(good_disk,"signed-ata-cold-reverify","update auth-stage",
          "MayOS: signed manifest + staged ATA data authenticated; NO INSTALL")
    with good_disk.open("r+b") as f:
        f.seek(STAGE_OFFSET+17004)
        b=f.read(1)
        assert len(b)==1
        f.seek(STAGE_OFFSET+17004)
        f.write(bytes([b[0]^0x01]))
    guest(good_disk,"signed-ata-postverify-tamper","update auth-stage",
          "Signed staging: signed digest differs from ATA staged payload")
    print("PASS: verified disk survives cold boot; tampering after verification rejected",flush=True)

    tampered=bytearray(signature)
    tampered[8]^=0x40
    bad_sig_disk=make_disk(BUILD/"signed-ata-bad-signature.img",valid,bytes(tampered))
    bad_sig_before=bad_sig_disk.read_bytes()
    guest(bad_sig_disk,"signed-ata-bad-signature","update auth-stream-stage-test",
          "Signed ATA stream: manifest/signature rejected (NO INSTALL)")
    assert bad_sig_disk.read_bytes()==bad_sig_before, "bad signature modified disk"
    assert_stage_blank(bad_sig_disk)
    print("PASS: corrupt Ed25519 signature refused before any disk write",flush=True)

    wrong_disk=make_disk(BUILD/"signed-ata-wrong-image-hash.img",wrong,wrong_sig)
    before=wrong_disk.read_bytes()[:MAYFS_END]
    guest(wrong_disk,"signed-ata-wrong-image-hash","update auth-stream-stage-test",
          "Signed ATA stream: body/hash verification failed (NO INSTALL)")
    assert wrong_disk.read_bytes()[:MAYFS_END]==before
    guest(wrong_disk,"signed-ata-wrong-image-hash-reverify","update auth-stage",
          "Signed staging: signed digest differs from ATA staged payload")
    print("PASS: a valid signature on the wrong image digest never authorizes staging as verified",flush=True)

    foreign_disk=make_disk(BUILD/"signed-ata-wrong-disk-capacity.img",valid,signature)
    with foreign_disk.open("r+b") as f:
        f.truncate(16*1024*1024)
    foreign_before=foreign_disk.read_bytes()
    guest(foreign_disk,"signed-ata-wrong-disk-capacity","update auth-stream-stage-test",
          "Signed ATA stream: isolated staging disk rejected (NO INSTALL)")
    assert foreign_disk.read_bytes()==foreign_before,"non-QEMU-sized disk changed"
    assert_stage_blank(foreign_disk)
    print("PASS: non-8MiB disk refused without writing any sector",flush=True)

if __name__=="__main__":
    main()

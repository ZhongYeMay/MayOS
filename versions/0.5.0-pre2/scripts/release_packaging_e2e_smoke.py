#!/usr/bin/env python3
"""End-to-end real QEMU release test using ephemeral *offline signed* files.

The MayKernel ELF64 must be built with the temporary CI PUBLIC key pinned
with MAYOS_RELEASE_SIGNATURE=1. The producer signs a 3-byte toy image; a real
ISO cannot fit current MayFS Update.bin (4096-byte cap), so NO installer is
enabled or tested. QEMU is booted from an ISO, not a Linux userspace mock.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct

from smoke import VM, BUILD
from rust_smoke import FS_COUNT, RECORD, snapshot


def disk_from_signed_release(path: Path, manifest: bytes, signature: bytes, payload: bytes):
    if not (0 < len(manifest) <= 2048 and len(signature) == 64 and 0 < len(payload) <= 4096):
        raise ValueError("end-to-end MayFS fixture requires bounded signed files")
    image = snapshot(manifest)
    records = bytearray(image[512:512 + FS_COUNT * RECORD])

    def put(index: int, name: bytes, content: bytes):
        base = index * RECORD
        struct.pack_into("<4I", records, base, 1, 0, 1, len(content))
        records[base + 16:base + 16 + len(name)] = name
        records[base + 64:base + 64 + len(content)] = content

    put(5, b"Update.sig", signature)
    put(6, b"Update.bin", payload)
    checksum = 2166136261
    for byte in records:
        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    image[512:512 + len(records)] = records
    struct.pack_into("<I", image, 8, checksum)
    with path.open("wb") as writer:
        writer.truncate(8 * 1024 * 1024)
        writer.seek(0)
        writer.write(image)
        writer.seek(512 * 512)
        writer.write(image)


def trial(label: str, manifest: bytes, signature: bytes, payload: bytes,
          expected: str, success: bool):
    disk = BUILD / f"release-e2e-{label}.img"
    disk_from_signed_release(disk, manifest, signature, payload)
    vm = VM(disk, f"maykernel-release-e2e-{label}")
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("update check")
        vm.wait("MayOS: Rust no_std manifest gate passed (UNTRUSTED)")
        vm.cmd("update auth")
        vm.wait(expected)
        if success:
            vm.wait("MayOS: local Ed25519 manifest signature and image digest verified; NO INSTALL")
        vm.shot(f"maykernel-release-e2e-{label}")
    finally:
        vm.stop()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--package-dir", type=Path, required=True)
    p.add_argument("--image", type=Path, required=True)
    args = p.parse_args()
    manifest = (args.package_dir / "Update.may").read_bytes()
    sig = (args.package_dir / "Update.sig").read_bytes()
    payload = args.image.read_bytes()
    trial("real-signed-fixture", manifest, sig, payload,
          "Authenticity: configured key + image verified, install DISABLED", True)
    corrupt_sig = bytearray(sig)
    corrupt_sig[7] ^= 0x01
    trial("modified-signature", manifest, bytes(corrupt_sig), payload,
          "Authenticity: Ed25519 signature REJECTED", False)
    corrupt_image = bytearray(payload)
    corrupt_image[0] ^= 0x01
    trial("modified-image", manifest, sig, bytes(corrupt_image),
          "Authenticity: signed metadata OK, image digest/size invalid", False)
    print("PASS: offline signing producer -> real MayKernel no_std Ed25519 QEMU verification (3 scenarios)", flush=True)
    print("PASS: payload/signature alteration denied; INSTALL remains DISABLED", flush=True)


if __name__ == "__main__":
    main()

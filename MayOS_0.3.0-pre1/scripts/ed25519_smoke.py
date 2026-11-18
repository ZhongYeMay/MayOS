#!/usr/bin/env python3
"""Actual QEMU MayKernel RustCrypto Ed25519 signatures and image hash checks.

The fixture key is RFC 8032 PUBLIC TEST DATA, universally known and NEVER
a production release root. No updater, disk install or HTTPS is enabled.
"""
import struct
from rust_smoke import MANIFEST, snapshot, FS_COUNT, RECORD
from smoke import VM, BUILD

# Signature over EXACT manifest bytes, generated offline with RFC 8032 vector 1.
# Its signing seed is widely published; it MUST NOT authenticate real releases.
TEST_SIGNATURE = bytes.fromhex(
    "a15757ec9751c7876f6425035f1a419c8f68871258223e189761a63b3c2cb96"
    "a9262f3dd1eb0a8c53255787fc7c7cd5d8ce0037ed3205fde45a0c1edb5b55b0f"
)

def test_disk(path, signature, image_data=b"abc"):
    if len(signature) != 64:
        raise ValueError("test signature must be 64 bytes")
    buffer = snapshot(MANIFEST)
    block = bytearray(buffer[512:512 + FS_COUNT * RECORD])

    def put(index, name, data):
        start = index * RECORD
        struct.pack_into("<4I", block, start, 1, 0, 1, len(data))
        block[start + 16:start + 16 + len(name)] = name
        block[start + 64:start + 64 + len(data)] = data

    put(5, b"Update.sig", signature)
    put(6, b"Update.bin", image_data)
    checksum = 2166136261
    for byte in block:
        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
    buffer[512:512 + len(block)] = block
    struct.pack_into("<I", buffer, 8, checksum)

    with path.open("wb") as out:
        out.truncate(8 * 1024 * 1024)
        out.seek(0)
        out.write(buffer)
        out.seek(512 * 512)
        out.write(buffer)

def run_case(tag, signature, payload, message, accepted):
    path = BUILD / (f"ed25519-{tag}.img")
    test_disk(path, signature, payload)
    vm = VM(path, f"maykernel-ed25519-{tag}")
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("update check")
        vm.wait("MayOS: Rust no_std manifest gate passed (UNTRUSTED)")
        vm.cmd("update auth")
        vm.wait(message)
        if accepted:
            vm.wait("MayOS: local Ed25519 manifest signature and image digest verified; NO INSTALL")
        vm.shot(f"maykernel-ed25519-{tag}")
        if accepted:
            # A successful earlier check must never authorize a subsequently
            # edited manifest; auth MUST re-parse signed input before use.
            vm.cmd("write Documents/Update.may invalid-metadata")
            vm.cmd("update auth")
            vm.wait("Authenticity: valid update metadata required first")
            vm.shot("maykernel-ed25519-stale-check-rejected")
    finally:
        vm.stop()

def main():
    run_case(
        "valid-test-key",
        TEST_SIGNATURE,
        b"abc",
        "Authenticity: TEST KEY match ONLY (not official), install DISABLED",
        True
    )
    bad_signature = bytearray(TEST_SIGNATURE)
    bad_signature[6] ^= 0x40
    run_case(
        "signature-tampered",
        bytes(bad_signature),
        b"abc",
        "Authenticity: Ed25519 signature REJECTED",
        False
    )
    run_case(
        "image-tampered",
        TEST_SIGNATURE,
        b"abd",
        "Authenticity: signed metadata OK, image digest/size invalid",
        False
    )
    print(
        "PASS: real MayKernel QEMU verifies test-only Ed25519 manifest "
        "and SHA-256 image; rejects signatures, image tampering and stale manifests; no install",
        flush=True
    )

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Boot actual MayKernel ELF64/QEMU with Rust no_std safety gate enabled.
Two separate MayFS disks demonstrate acceptance of untrusted compatible
metadata and fail-closed rejection of duplicated fields.
This is NOT a release signature or installer test.
"""
from pathlib import Path
import struct
from smoke import VM, BUILD

FS_COUNT = 48
RECORD = 4160
MANIFEST = (
    b"name=MayOS\nversion=0.5.0-pre3\narchitecture=x86_64\n"
    b"kernel=MayKernel\nrelease_date=2026-11-18\nsize_bytes=3\n"
    b"sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
    b"eta_minutes=1\nchangelog=Rust C ABI test\n"
)

def snapshot(manifest):
    records = bytearray(FS_COUNT * RECORD)

    def add(index, name, parent, directory, data=b""):
        assert len(name) < 48 and len(data) < 4096
        pos = index * RECORD
        struct.pack_into("<4I", records, pos, 1, int(directory), parent, len(data))
        records[pos+16:pos+16+len(name)] = name
        records[pos+64:pos+64+len(data)] = data

    add(0, b"disk1", 0, True)
    add(1, b"Documents", 0, True)
    add(2, b"Desktop", 0, True)
    add(3, b"Trash", 0, True)
    add(4, b"Update.may", 1, False, manifest)

    # MayFS uses an FNV-1a hash over exactly sizeof(File[48]) data.
    fnv = 2166136261
    for byte in records:
        fnv = ((fnv ^ byte) * 16777619) & 0xffffffff
    output = bytearray(512 * 512)
    output[:8] = b"MAYFS001"
    struct.pack_into("<I", output, 8, fnv)
    output[12:16] = b"MAY2"
    struct.pack_into("<II", output, 16, 1, (~1) & 0xffffffff)
    output[512:512+len(records)] = records
    return output

def write_image(path, manifest):
    data = snapshot(manifest)
    with path.open("wb") as f:
        # 8 MiB throwaway isolated test disk; two equal MayFS snapshots.
        f.truncate(8 * 1024 * 1024)
        f.seek(0)
        f.write(data)
        f.seek(512 * 512)
        f.write(data)

def main():
    valid = BUILD / "rust-c-ffi-valid.img"
    invalid = BUILD / "rust-c-ffi-duplicate.img"
    write_image(valid, MANIFEST)
    write_image(invalid, MANIFEST + b"name=MayOS\n")
    vm = VM(valid, "maykernel-rust-valid")
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("update check")
        vm.wait("MayOS: Rust no_std manifest gate passed (UNTRUSTED)")
        vm.wait("Newer version found (UNVERIFIED metadata)")
        vm.shot("maykernel-rust-c-valid-metadata")
    finally:
        vm.stop()
    vm = VM(invalid, "maykernel-rust-duplicate")
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("update check")
        vm.wait("MayOS: Rust update manifest gate rejected untrusted metadata")
        vm.wait("Manifest invalid or incompatible (no changes made)")
        vm.shot("maykernel-rust-c-duplicate-rejected")
    finally:
        vm.stop()
    print("PASS: native x86_64 MayKernel Rust no_std/C ABI accepted bounded untrusted metadata and rejected duplicate field", flush=True)

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Actual QEMU proof that Rust no_std update guard and upstream BearSSL
engine coexist in the SAME native MayKernel ELF64 without enabling HTTPS.
"""
from rust_smoke import MANIFEST, write_image
from smoke import VM, BUILD

def main():
    disk = BUILD / "maykernel-rust-bearssl.img"
    write_image(disk, MANIFEST)
    vm = VM(disk, "maykernel-rust-bearssl-linked")
    try:
        vm.wait("MayOS: BearSSL TLS engine structures initialized (NO TRUST/NO HANDSHAKE)")
        vm.wait("MayOS: TLS blocked: production certificate trust and verified time unavailable")
        vm.key("f1")
        vm.cmd("update check")
        vm.wait("MayOS: Rust no_std manifest gate passed (UNTRUSTED)")
        vm.wait("Newer version found (UNVERIFIED metadata)")
        vm.cmd("tls status")
        vm.wait("BearSSL TLS engine: linked for isolated testing ONLY")
        vm.wait("MayKernel HTTPS: DISABLED (no authenticated TLS handshake)")
        vm.cmd("tls check api.github.com")
        vm.wait("TLS BLOCKED: cryptographic DRBG is unavailable")
        vm.shot("maykernel-rust-c-bearssl-coexist")
        print("PASS: genuine Rust+C+BearSSL MayKernel QEMU coexist; update metadata untrusted and HTTPS fail-closed", flush=True)
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

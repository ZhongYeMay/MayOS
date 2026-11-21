#!/usr/bin/env python3
"""Native MayKernel signed HTTP-body stream acceptance (no Internet or install).

CI generates a new ephemeral Ed25519 signing key, packages an actual 64KiB
image, and pins ONLY its public key into the Rust+C x86_64 kernel.
The guest verifies that signature before streaming a deterministic fixture.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
from signed_stage_e2e_smoke import make_disk, change_signature
from smoke import VM, BUILD

def run_case(name: str, manifest: bytes, sig: bytes, accepted: bool, reason: str):
    disk=make_disk(BUILD/(name+".img"),manifest,sig)
    before=disk.read_bytes()[:2*512*512]
    vm=VM(disk,name)
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.key("f1")
        vm.cmd("update auth-stream-test")
        vm.wait(reason)
        if accepted:
            vm.wait("MayOS: Ed25519 authenticated manifest + 64KiB streamed SHA-256 PASS (NO INSTALL)")
        vm.wait("No download/install; rollback-capable installer not ready.")
        vm.shot(name)
        assert disk.read_bytes()[:2*512*512]==before, "MayFS changed in read-only stream test"
        if accepted:
            # After a valid stream, replace the previously signed manifest.
            # A second operation MUST re-parse bytes and fail closed.
            vm.cmd("write Documents/Update.may invalid-metadata")
            vm.cmd("update auth-stream-test")
            vm.wait("Signed stream: local manifest invalid (NO INSTALL)")
            vm.shot(name+"-stale-manifest-rejected")
            print("PASS: previous signed stream cannot authenticate a modified MayFS manifest",flush=True)
    finally:
        vm.stop()
    print("PASS: "+name+" accepted="+str(accepted)+"; MayFS untouched; NO INSTALL",flush=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--package-dir",required=True,type=Path)
    parser.add_argument("--wrong-hash-package-dir",required=True,type=Path)
    args=parser.parse_args()
    good=(args.package_dir/"Update.may").read_bytes()
    signature=(args.package_dir/"Update.sig").read_bytes()
    bad_hash=(args.wrong_hash_package_dir/"Update.may").read_bytes()
    bad_hash_signature=(args.wrong_hash_package_dir/"Update.sig").read_bytes()
    assert len(signature)==len(bad_hash_signature)==64
    assert good != bad_hash and b"size_bytes=65536" in good and b"size_bytes=65536" in bad_hash
    run_case("signed-stream-valid",good,signature,True,
             "Signed stream: Ed25519 + SHA-256 PASS (NO INSTALL)")
    changed=bytearray(signature)
    changed[8]^=0x80
    run_case("signed-stream-signature-tampered",good,bytes(changed),False,
             "Signed stream: Ed25519 signature REJECTED (NO INSTALL)")
    run_case("signed-stream-hash-tampered",bad_hash,bad_hash_signature,False,
             "Signed stream: streamed SHA-256 REJECTED (NO INSTALL)")
    print("PASS: real RustCrypto Ed25519 signed manifest -> native Rust+C 64KiB SHA-256",flush=True)
    print("PASS: damaged signature and independently signed but incorrect digest both rejected",flush=True)

if __name__=="__main__":
    main()

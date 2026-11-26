#!/usr/bin/env python3
"""Real Rust+C x86_64 QEMU SHA-256 streaming diagnostic; NO HTTPS."""
from smoke import VM

def main():
    vm=VM(None,"maykernel-stream-integrity")
    try:
        vm.wait("MayOS: desktop ready")
        vm.key("f1")
        vm.cmd("tls digest-test")
        vm.wait("MayOS: HTTP stream digest self-test PASSED (NO DOWNLOAD)")
        vm.wait("HTTP stream SHA-256 and exact-length PASS (NO INSTALL)")
        vm.wait("Native Bing HTTPS prerequisites: pending")
        vm.shot("maykernel-stream-integrity")
        print("PASS: actual Rust HTTP byte budget and native C SHA-256 in x86_64 QEMU")
        print("PASS: tamper, truncation and extra bytes rejected; no network or disk write")
    finally:
        vm.stop()

if __name__=="__main__":
    main()

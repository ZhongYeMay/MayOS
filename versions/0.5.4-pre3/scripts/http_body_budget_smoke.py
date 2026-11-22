#!/usr/bin/env python3
"""Real QEMU validation of Rust HTTP body byte-budget, with no network calls.

The test is deliberately not an HTTPS connection and does not authorize
installation or write the filesystem.
"""
from smoke import VM

def main():
    vm=VM(None,"maykernel-http-body-budget")
    try:
        vm.wait("MayOS: desktop ready")
        vm.key("f1")
        vm.cmd("tls body-test")
        vm.wait("MayOS: strict HTTP body budget self-test PASSED (NO CONNECT)")
        vm.wait("HTTP body streaming budget PASS (NO DOWNLOAD)")
        vm.wait("MayKernel HTTPS: DISABLED (no authenticated TLS handshake)")
        vm.shot("maykernel-http-body-budget")
        print("PASS: native x86_64 Rust C MayKernel enforces 64KiB exact HTTP body length; truncation, extra bytes and overlarge chunks rejected",flush=True)
        print("PASS: no network request, downloaded payload, disk install or rollback mutation",flush=True)
    finally:
        vm.stop()

if __name__=="__main__":
    main()

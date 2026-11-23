#!/usr/bin/env python3
"""Real QEMU Rust no_std strict GitHub Release URL/redirect policy check.

This is a security prerequisite ONLY: it neither uses DNS nor opens TLS.
"""
from smoke import VM

def main():
    vm = VM(None, "maykernel-release-url-preflight", nic=False)
    try:
        vm.key("f1")
        vm.cmd("tls url-test")
        vm.wait("MayOS: GitHub Release URL policy self-test PASSED (NO CONNECT)")
        vm.wait("GitHub Release URL policy PASS (offline; NO HTTPS)")
        vm.wait("Native Bing HTTPS prerequisites: pending")
        vm.shot("maykernel-release-url-preflight")
        print("PASS: native Rust+C kernel accepts exact GitHub Release HTTPS origins and rejects unsafe redirects", flush=True)
        print("PASS: no TCP connection, DNS request, certificate trust, download or installation was enabled", flush=True)
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

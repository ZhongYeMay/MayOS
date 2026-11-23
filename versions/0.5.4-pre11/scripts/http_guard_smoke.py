#!/usr/bin/env python3
"""Actual QEMU MayKernel Rust no_std strict HTTP response framing test.

No TLS handshake or network request is made; this only rejects ambiguous
server data before a future verified GitHub Release streaming downloader.
"""
from smoke import VM

def main():
    vm=VM(None,"maykernel-http-guard-qemu")
    try:
        vm.key("f1")
        vm.cmd("tls http-test")
        vm.wait("MayOS: HTTP framing strict no_std self-test PASSED (NO CONNECT)")
        vm.wait("HTTP/1.1 update response preflight PASS (NO CONNECT)")
        vm.wait("Native Bing HTTPS prerequisites: pending")
        vm.shot("maykernel-http-release-framing")
        print("PASS: native Rust+C MayKernel checks strict HTTP length, redirect, smuggling and downgrade negatives",flush=True)
        print("PASS: network TLS, GitHub request and installer still DISABLED",flush=True)
    finally:
        vm.stop()

if __name__=="__main__":
    main()

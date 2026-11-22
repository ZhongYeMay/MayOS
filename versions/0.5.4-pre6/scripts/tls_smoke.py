#!/usr/bin/env python3
"""Native QEMU TLS preflight: deliberately fail closed without a verified
X.509/TLS provider and prove hostile hostnames cannot trigger downloads.
There is NO TLS handshake in this test.
"""
from smoke import VM


def main():
    vm = VM(None, "maykernel-tls-fail-closed", nic=True)
    try:
        vm.wait("MayOS: TLS blocked: production certificate trust and verified time unavailable")
        vm.key("f1")
        vm.cmd("tls status")
        vm.wait("MayKernel HTTPS: DISABLED (no authenticated TLS handshake)")
        vm.wait("BearSSL TLS engine: not linked in this build")
        vm.wait("Trusted TLS time: unavailable")
        vm.wait("Trusted CA roots: not installed")
        vm.cmd("tls check api.github.com")
        vm.wait("TLS BLOCKED: cryptographic DRBG is unavailable")
        vm.wait("MayOS: TLS preflight fail-closed (no HTTPS request sent)")
        vm.cmd("tls check api.github.com.evil")
        vm.wait("TLS BLOCKED: update host must be api.github.com or github.com")
        vm.shot("maykernel-tls-security-fail-closed")
        print("PASS: real QEMU MayKernel hardware entropy/RTC probe; HTTPS fail-closed and hostname policy", flush=True)
    finally:
        vm.stop()


if __name__ == "__main__":
    main()

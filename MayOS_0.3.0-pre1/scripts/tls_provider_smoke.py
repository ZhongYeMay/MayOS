#!/usr/bin/env python3
"""Real native QEMU proof of a LINKED BearSSL TLS engine (not HTTPS).
The engine has no trust anchors, no authenticated time, no secure DRBG, and
MUST refuse verified update connections.
"""
from smoke import VM

def main():
    vm=VM(None, "maykernel-bearssl-linked", nic=True)
    try:
        vm.wait("MayOS: BearSSL TLS engine structures initialized (NO TRUST/NO HANDSHAKE)")
        vm.wait("MayOS: TLS blocked: no trusted time, CSPRNG or X.509 provider")
        vm.key("f1")
        vm.cmd("tls status")
        vm.wait("BearSSL TLS engine: linked for isolated testing ONLY")
        vm.wait("MayOS: BearSSL scalar-only AEAD backend selected (NO TRUST/NO HTTPS)")
        vm.wait("Bare-metal cipher: portable ChaCha20/Poly1305 (NO HTTPS)")
        vm.wait("MayKernel HTTPS: DISABLED (no authenticated TLS handshake)")
        vm.cmd("tls check api.github.com")
        vm.wait("TLS BLOCKED: cryptographic DRBG is unavailable")
        vm.shot("maykernel-bearssl-engine-linked-no-https")
        print("PASS: genuine BearSSL native ELF64 init in QEMU; authenticated HTTPS remains blocked",flush=True)
    finally:
        vm.stop()

if __name__=="__main__":
    main()

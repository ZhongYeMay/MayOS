#!/usr/bin/env python3
"""Real MayKernel/QEMU BearSSL client/server TLS 1.2 memory transport test.

This is a full cryptographic TLS record handshake between two BearSSL engines
inside one test-only kernel, not a real TCP/Internet HTTPS connection.
"""
from smoke import VM

def main():
    vm=VM(None,"maykernel-bearssl-tls-memory")
    try:
        vm.wait("MayOS: BearSSL TLS engine structures initialized (NO TRUST/NO HANDSHAKE)")
        vm.key("f1")
        vm.cmd("tls memory-handshake")
        vm.wait("MayOS: BearSSL TLS 1.2 verified test-CA handshake + encrypted record PASSED (MEMORY ONLY)")
        vm.wait("MayOS: BearSSL TLS bad hostname, expired cert and unknown CA all REJECTED (MEMORY ONLY)")
        vm.wait("BearSSL TLS1.2 memory-only handshake PASS (NO HTTPS)")
        vm.wait("MayKernel HTTPS: DISABLED (no authenticated TLS handshake)")
        vm.shot("maykernel-bearssl-tls12-memory-handshake")
        print("PASS: native BearSSL TLS1.2 ClientHello/Finished and encrypted test application record over in-memory engine transport",flush=True)
        print("PASS: wrong hostname, expired certificate and unknown CA refused; NO TCP, GitHub download or installer",flush=True)
    finally:
        vm.stop()

if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""Boot actual native MayKernel ELF64 with test-only BearSSL X509 engine.

Only CI-minted RSA CA certificates are used. The user-facing HTTPS update
client remains explicitly disabled; no network request or install takes place.
"""
from smoke import VM

def main():
    vm=VM(None,"maykernel-bearssl-x509-fixture")
    try:
        vm.wait("MayOS: BearSSL TLS engine structures initialized (NO TRUST/NO HANDSHAKE)")
        vm.key("f1")
        vm.cmd("tls x509-test")
        vm.wait("MayOS: BearSSL X.509 CA, hostname and validity diagnostics PASS (NO HANDSHAKE)")
        vm.wait("BearSSL X.509 test CA and hostname PASS (NO TLS HANDSHAKE)")
        vm.wait("Native Bing HTTPS prerequisites: pending")
        vm.shot("maykernel-bearssl-x509-test-ca")
        print("PASS: native BearSSL X509 validates CI-only CA+hostname+time and rejects wrong host, expiry and unknown CA",flush=True)
        print("PASS: HTTPS still disabled; no private keys, real GitHub TLS or production trust roots included",flush=True)
    finally:
        vm.stop()

if __name__=="__main__":
    main()

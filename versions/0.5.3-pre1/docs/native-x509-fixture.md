# Stage 5C1 — native BearSSL X.509 chain, hostname and expiry checks (ephemeral test CA only)

## Objective

Verify that the actual freestanding MayKernel can invoke real BearSSL X.509 trust-anchor and hostname verification with certificates, rather than merely link a TLS engine.

The diagnostic is **strictly opt-in** with `MAYOS_TLS_X509_DIAGNOSTIC=1` and pinned upstream BearSSL source. GitHub Actions creates an ephemeral RSA-2048 CA and a signed server certificate for `test.mayos.local` using OpenSSL, exports their public certificate DER, extracts a test CA subject and RSA public key, compiles them into a C-only fixture in the native x86_64 ELF64, and boots it under QEMU.

The MayShell `tls x509-test` path invokes real `br_x509_minimal_init_full`, `br_x509_minimal_set_time` and the X.509 engine's certificate-processing callbacks. The test requires: positive match of CA+hostname+time, rejection of wrong host, rejection after expiration, and rejection when there are no trust anchors. A sample certificate is fed in chunks. All tests must pass together.

## Security limitations

- This is a **CI-only test CA**, not an official GitHub or MayOS public trust root. CA private keys remain in GitHub runner TEMP and are never committed.
- The test explicitly injects the **CI clock** into BearSSL; it is not trusted time for a real device.
- No actual TLS ClientHello, handshake, session keys, TCP transport or HTTP request is performed, even if the X.509 checks pass.
- BearSSL's current pinned revision is experimental. A maintained and security-reviewed upstream version is mandatory before public release.
- MayKernel's production `may_tls_update_preflight` remains fail-closed and all HTTPS update/install operations remain blocked.

## Repeat test

Run the dedicated CI workflow or provide your own ephemeral CA+leaf generated in the same way, and a *pinned* BearSSL source checkout. The repository's workflow is authoritative for QEMU/serial/screenshot evidence. No production CA trust is ever enabled.

Follow-on work: trustworthy entropy/CSPRNG, device time/CA provisioning and certificate revocation policy, a real TLS record transport atop a reliable generalized network stack, and strict negative handshake tests. Only then allow GitHub Release streaming/download and signed image installation.
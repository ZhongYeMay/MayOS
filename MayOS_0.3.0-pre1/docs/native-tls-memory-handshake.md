# Stage 5C2: true BearSSL TLS 1.2 handshake inside MayKernel (memory-only diagnostic)

## Engineering objective

The prior stage proved real BearSSL X.509 verification independently of a TLS record layer. This opt-in CI diagnostic runs BOTH BearSSL client and server engines inside actual freestanding x86_64 MayKernel/QEMU. They exchange genuine encrypted TLS 1.2 protocol records through a bounded 1024-byte in-memory transport. This tests actual ClientHello/Finished handshake and application-record encryption/decryption without relying on any new TCP/TLS network driver.

## Strict test-only conditions

- Enabled exclusively by MAYOS_TLS_HANDSHAKE_DIAGNOSTIC=1 and an explicitly pinned upstream BearSSL checkout.
- CI generates disposable RSA-2048 CA and server certificates, a temporary RSA CRT private key embedded in the **CI-only test kernel**, and distinct ephemeral seed bytes for both engines. The private key is NOT a MayOS release signing key, and is never committed in source.
- Trusted certificate time is CI test data, NOT a device's trusted clock. Neither the test CA nor the test random bytes are production trust/entropy sources.
- Both engines are restricted to TLS 1.2. The client must validate the test CA, hostname and certificate validity through BearSSL as part of the handshake. Positive test additionally sends an application plaintext string through an encrypted TLS record, verified at the server.
- Wrong hostname, expired leaf and zero trust anchors must all cause handshake refusal.
- No TCP socket, real GitHub DNS, HTTPS, network records, download, installer or bootloader write is performed. MayKernel's ordinary TLS security preflight continues to BLOCK production update connectivity.

## Reproducibility and evidence

CI workflow: .github/workflows/maykernel-tls-memory-handshake.yml. Guest command: tls memory-handshake. It runs the actual BearSSL client and server code inside a QEMU-booted native MayKernel and exports real framebuffer and serial logs.

## Remaining before a secure updater

A generalized reliable streaming TCP socket; BearSSL record I/O connected to that socket; a validated hardware entropy source with CSPRNG; trustworthy time and CA root management; real GitHub Release API and asset HTTP framing; signed manifest anti-rollback; true block storage/partition writer with bootloader A/B update and power-failure tests. This test-only memory handshake cannot replace any of these release gates.
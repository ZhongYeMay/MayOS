# Stage 6G — HTTP body byte budget and ATA reliability

This is a strict Rust no_std length accounting module for FUTURE authenticated TLS update transfers. It is NOT a network client, signature verifier, disk writer or installer.

- begin: require HTTP 200, an expected signed size exactly matching declared Content-Length, and bounded size 1..524288 bytes.
- consume: reject zero-sized chunks, chunks larger than 4096 bytes, excess content and invalid or completed state.
- finish: accept only exact declared length; truncated transfer and early EOF are rejected permanently.
- Pure Rust C ABI and real MayKernel QEMU shell command tls body-test run without any network connection.

The independent isolated ATA regression fixes a possible race: previously, the driver checked only !BSY before flushing, while DRQ could still be asserted. Now it waits for DRQ to clear and emits separate I/O failure diagnostics for header write, flush and readback. Only explicit test builds can write the marked, exact 8MiB QEMU disk.

Both the no_std body budget and QEMU ATA multi-boot regression run in independent CI jobs. Passing them is not proof of a real HTTPS client or safe installer.

Remaining: authenticated TLS handshake with real trust anchors, secure time and entropy, reliable TCP streaming, signed production release source, large partition allocator and bootloader A/B rollback.
# MayOS / MayKernel: staged Rust + C hybrid architecture

## Decision

Retain the tested C native MayKernel boot, PCI RTL8139, TCP/DHCP/DNS, MayFS, graphics, existing optional BearSSL code and their unchanged behavior. Adopt Rust gradually for memory-sensitive core modules by isolating explicit C ABI boundaries.

## First genuine Rust module

- rust/maykernel_policy/src/lib.rs is a real Rust 2021 **no_std**, no-heap parser checking untrusted Documents/Update.may metadata.
- The Rust function mayrs_manifest_gate has C ABI: const uint8_t pointer + 32-bit length -> int. It checks all required keys exactly once, bounded string sizes, accepted product/architecture/kernel, numeric overflow, version syntax, SHA-256 hex syntax and valid Gregorian dates.
- Rust explicitly rejects duplicates, malformed input, NUL bytes and oversized buffers. The C parser runs afterwards. Rust validation and SHA-256 are NOT publisher signatures.
- C calls Rust only when MAYOS_RUST=1; default C-only build is unchanged. No boot, GUI, NIC or MayFS ABI redesign.
- Rust staticlib targets x86_64-unknown-none with panic=abort, no red-zone, no_std and no allocator. Rust compiler pinned in CI to 1.85.1 (target x86_64-unknown-none).

## Build and test

From MayOS_0.3.0-pre1 with rustup and QEMU dependencies:

```sh
rustup toolchain install 1.85.1 --profile minimal --target x86_64-unknown-none
rustup default 1.85.1
make test-rust-host
make clean                  # mandatory when switching C-only/Rust build flags
make MAYOS_RUST=1 test-rust-qemu
```

The two QEMU boots use isolated 8 MiB test disks containing checksummed MayFS snapshots. One valid metadata file must be accepted as untrusted; one duplicate field must be rejected before it reaches the legacy C parser. Both tests capture real framebuffer and serial evidence.

## Safety / compatibility

- No updates, bootloaders or partitions are written by Rust; no safe HTTPS/verified release download or A/B install has been implemented.
- No automatic rewriting of existing drivers from C to Rust until behavior-specific tests exist.
- The approved native black/gray GUI remains unchanged. No webview, Linux userspace dependency or false video/screenshot claims.
- The FFI pointer must be valid for the length provided, bounded by the MayFS file allocation; no Rust ownership of C memory is transferred.
- Rust incremental memory safety does not itself make unauthenticated update metadata trustworthy.

## Migration milestones

1. Rust no_std compile + FFI and QEMU boot success.
2. Move independent update/network parsing into typed Rust APIs, keeping stable C ABIs.
3. Introduce a Rust-owned allocator only after QEMU memory regressions and explicit allocation budgets.
4. Gradually move memory management/scheduler/filesystem security-sensitive portions under validated interfaces; don't rewrite the whole OS in one change.
5. Integrate and verify a mature TLS engine with trustworthy entropy, time and CA certificates; no custom unaudited TLS protocol.

PR for this phase must not merge until pinned Rust + C QEMU CI passes.
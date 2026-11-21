# MayKernel large image staging — guarded real ATA integration milestone

## Problem and scope

The existing MayFS stores individual files in 4096-byte records. A production MayOS ISO is much larger, so `Documents/Update.bin` cannot be used for full-image downloads. This change adds a **generic bounded sequential sector-streaming staging backend** with two crash-detecting progress headers, and a separate **64 KiB deterministic QEMU test fixture** to prove it can write, resume and hash content larger than MayFS file limits.

**This is NOT GitHub Release downloading, HTTPS, signed update authentication, image installation or A/B rollback.** Default builds have no staging writer, and the only exposed test input is a local deterministic fixture. The approved desktop design remains unchanged.

## Exact sandbox boundary

The diagnostic only exists with `MAYOS_STAGE_DISK_DIAGNOSTIC=1`. It refuses all but the **8 MiB primary ATA test disk** containing the exact marker `Documents/STAGE-DIAGNOSTIC.marker` with contents `ISOLATED-QEMU-UPDATE-STAGE-ONLY`.

| LBA | Use |
| --- | --- |
| 0–1023 | Existing primary/secondary MayFS snapshot (**never touched**) |
| 1024–1025 | Previous A/B journal diagnostic (**never touched**) |
| 1536–1537 | Two 512-byte stream-progress headers |
| 2048–3071 | Maximum 512 KiB image staging data |
| Other | **Never touched** |

A fresh stage requires both header sectors and the entire reserved data region to be blank; foreign sector contents cause refusal. Every chunk must be <=4096 bytes, written in increasing offset order and sector-aligned except a final partial sector. The driver performs ATA FLUSH CACHE after payload writes, confirms the read-back bytes, then commits a new generation progress header to the **other** header sector, flushes and reads back that header. Interrupted tail writes can be safely retried after reading the most recently committed header.

The first 56 bytes of the staged header are protected by an FNV checksum and the rest of the 512-byte header is required to be zero. FNV detects accidental torn writes only, not malicious modification. A SHA-256 is calculated from actual disk bytes after staging completes, compared to the expected digest, then the header is marked `HASH_VALID`. **This is only a content integrity check, not a signature or publisher trust assertion.**

## Tests

```sh
make test-stage-codec
rustup toolchain install 1.85.1 --profile minimal --target x86_64-unknown-none
rustup default 1.85.1
make clean
make MAYOS_STAGE_DISK_DIAGNOSTIC=1 test-stage-qemu
```

The real QEMU scenario starts the native Rust+C MayKernel, streams 64 KiB (16 × 4096-byte chunks), performs multiple cold boots and resumes from 32 KiB stored bytes. It then verifies the actual ATA image bytes against a host-generated independent fixture, tests that a corrupted payload fails SHA-256, and that the uncorrupted disk enters a hash-verified state that survives another cold reboot. Tests also compare all existing MayFS sectors byte-for-byte and verify that unmarked disks and disks with foreign reserved-sector data are untouched. The attached screenshots are from actual QEMU guest framebuffer and serial logs, not mockups.

## Production blockers

- A properly partitioned, large-capacity block store with per-device ownership and filesystem/bootloader isolation; current test limits 512 KiB.
- Authenticated HTTPS and HTTP redirect restrictions for GitHub Release, real DNS and reliable streaming socket APIs (current network is QEMU fixture-driven).
- Publisher-signed manifest, metadata anti-rollback and verification of final bytes against a securely pinned public key at the commit boundary.
- A/B partition write path with measured safety, bootloader trial slot selection and genuine power-loss recovery.
- Hardware and physical storage testing. No signed install should run without these gates.

**Never enable the diagnostic switch in production or use its SHA-256 status as proof an image came from MayOS.**

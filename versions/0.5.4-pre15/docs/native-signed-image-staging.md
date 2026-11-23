# Stage 6D — authenticated staged-image verification in native MayKernel

## Resulting flow (diagnostic-only)

1. An **external offline issuer** generates a MayOS `Update.may` with image length and SHA-256, then signs its exact bytes using Ed25519 to produce `Update.sig`. The issuer's matching public key must be pinned **inside a trusted MayKernel build**, never downloaded with the manifest.
2. The native diagnostic-only ATA stage stores a large payload in 512-byte sectors (current QEMU fixture 64KiB) and persists a journal of committed bytes, independently of MayFS's ~4KiB per-file limit.
3. MayKernel's `update stage-verify` performs a **complete ATA readback** and SHA-256 check against the staging header, marking the data locally `HASH_VALID`. This is an integrity check only, not publisher authentication.
4. The new `update auth-stage` **re-reads and reparses the actual current MayFS manifest**, verifies the detached Ed25519 signature under the build-pinned key, checks size/hash against staged metadata, and **re-reads and re-hashes ALL staged disk data again**. A modified payload after a previous hash check is rejected.
5. It prints `Signed staging: Ed25519 + full disk image matched; NO INSTALL`, never writes a boot partition, never changes a GRUB/Limine entry, and never performs an update.

## Test-only guard

The actual signed staging path is compiled only when **both** `MAYOS_RELEASE_SIGNATURE=1` and `MAYOS_STAGE_DISK_DIAGNOSTIC=1` are set. It still requires the exact isolated 8MiB QEMU test disk and MayFS marker. Ordinary, unsigned or non-diagnostic builds fail closed. CI generates an ephemeral signing key and only pins its **public key** into the test ISO; no signing private key is included in any artifact or repository.

## Reproducible proof

CI creates an actual 65536-byte test image, signs it with the existing `package_signed_release.py` producer, verifies it independently using OpenSSL, then boots a genuine x86_64 Rust+C MayKernel with the matching public key and ATA test drive. The QEMU tests cover:

- Valid signed image staged across two cold-boot phases, verified from ATA and authenticated under the pinned test public key.
- A valid signed manifest while the staged image is incomplete: **reject**.
- A changed Ed25519 signature after successful staging: **reject**.
- A changed ATA data byte after an earlier successful staging hash: **reject through a fresh full-disk SHA-256 read**.
- No MayFS snapshot modification. No installation or bootloader write is possible through this interface.

This integration is more substantial than verifying a tiny `Documents/Update.bin`: signed metadata now covers a large payload stored outside MayFS, but it does **not** make a user-ready network updater.

## Remaining release-blocking work

Trusted HTTPS (DRBG entropy, trustworthy certificate time, up-to-date CA roots, strict X.509 host verification), GitHub Release real streaming transfer with path/redirect/size limits and retries, signed version/epoch monotonic policy, real inactive bootable partition writer, bootloader slot selection and health-confirmed rollback, durable power-loss testing and owner-managed signing-key lifecycle. The current staging area is 512 KiB and the fixture is synthetic, not a full system ISO. Never promote this diagnostic to a public installer.

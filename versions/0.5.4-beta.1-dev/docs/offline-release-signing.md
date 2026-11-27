# Offline release packaging: actual Ed25519 signing (not installer)

MayOS now has an **offline release producer** matching the existing Rust/C native `Update.may` and `Update.sig` verifier. It signs metadata about a real image, including its byte count and streaming SHA-256, with an owner-managed Ed25519 private key via OpenSSL. The emitted raw manifest bytes are the exact Ed25519 message. The private key is never copied into the release directory, and the public key must be pinned **separately** in the trusted MayKernel image.

## Operator example

```sh
# Example only: the secret must be owner-controlled and stored OFFLINE.
python3 scripts/package_signed_release.py \
  --image /build/release/MayOS-test.iso \
  --private-key /secure-offline/owner-ed25519.pem \
  --output-dir /build/release/new-signing-output \
  --version 0.5.0-pre3 --date 2026-11-19 \
  --eta-minutes 20 --changelog 'MayOS preview fixes'
```

The tool writes `Update.may` and binary `Update.sig` to a previously empty release directory. It does not alter or copy the image. The output directory intentionally must differ from the signing key directory. OpenSSL independently verifies the signature before publishing it.

## Security properties

- Release date and version follow the native C/Rust parser expectations; the manifest is ASCII, bounded to 2048 bytes, with exactly the nine expected fields and LF line endings.
- Image hashes are calculated with bounded memory chunks; current MayKernel metadata limits file size to 32-bit `size_bytes` (<= 4 GiB).
- Ed25519 is over **the complete raw `Update.may` bytes**, which include SHA-256 of the image. This is not `Ed25519(SHA256(Update.may))`.
- Never use a CI-generated disposable test key as an official release root. Only an owner-approved, independently trusted public key compiled into MayKernel provides release authenticity.
- Producing an `Update.may` + signature is NOT proof of provenance if it is verified against a public key supplied by untrusted media.

## Tests

```sh
python3 scripts/test_package_signed_release.py
```

The test generates ephemeral Ed25519 keys in a temporary directory, signs a small fixture, verifies with an independently run OpenSSL public-key verifier, and rejects modifications to signatures and metadata. It checks the signed digest no longer matches a tampered image and invalid metadata does not produce a signed package. **No private signing keys or QEMU images are committed.**

## Remaining functionality

This packaging feature is **not a system updater**. Current MayFS `Update.bin` is limited to 4096 bytes; it cannot hold a real ISO image. Full GitHub Release transport still requires authenticated HTTPS, streaming/chunked download to a bounded block store, signature trust, monotonic rollback protection, and an A/B installer/bootloader update path. This offline sign step only makes the release metadata format interoperable with the previously tested signature verifier.
## Read-only verification before any future installation

The inverse operation uses an independently trusted public key stored **outside** the downloaded package. It reads the actual image, checks that exact signed metadata was produced by the trusted key, enforces the nine canonical MayKernel fields, and recalculates the full image digest:

```sh
python3 scripts/verify_signed_release.py \
  --image /download/MayOS-preview.iso \
  --manifest /download/Update.may \
  --signature /download/Update.sig \
  --trusted-public-key /trusted/owner-release.pub.pem
```

The verifier rejects a public key bundled in the same untrusted download directory, malformed metadata, altered signatures, and an ISO whose size or hash differs from the signed manifest. **Read-only verification cannot establish safe disk install readiness**: it does not supply protected trusted boot, anti-rollback, a verified transport or an A/B installer.

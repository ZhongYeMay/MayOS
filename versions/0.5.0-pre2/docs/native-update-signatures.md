# MayKernel signed update authentication — Rust + C hybrid

## Scope (what is actually implemented)

- Optional **RustCrypto Ed25519** detached signature verification via a real Rust no_std static library, not a handwritten cipher.
- The release public key is pinned at OS **build time**, never downloaded from a manifest or requested from an attacker-controlled URL.
- The MayKernel command `update check` validates metadata structure; `update auth` then verifies a 64-byte `Documents/Update.sig` over **the exact raw bytes** of `Documents/Update.may` and rehashes actual `Documents/Update.bin` to match the authenticated SHA-256 and size.
- Missing/invalid key, bad metadata, missing signature, altered signature, changed image size or digest cause fail-closed denial.
- No download, no boot record writes, no installation and no grant of trust based only on unsigned SHA-256.

## Trust root configuration (required before anyone can claim a signed release)

Generate a new Ed25519 signing key *outside GitHub CI and outside the repository* using established key management. Keep the **private signing key exclusively with the project owner**; do not paste it into this chat, commit it, or put it in a firmware image. Distribute and embed only its 32-byte public key in trusted release builds. Sign Update.may raw bytes offline and save the detached 64-byte signature as Documents/Update.sig.

```sh
# Requires rustup with x86_64-unknown-none installed, and cargo dependencies.
make clean
make MAYOS_RELEASE_SIGNATURE=1 MAYOS_RELEASE_PUBKEY_HEX=<64-hex-official-public-key> all
```

The above **only makes signature checking available**. It does not ship an updater, TLS, installer or rollback. The Makefile defaults to `MAYOS_RELEASE_SIGNATURE=0` with no pinned trust root; therefore attempts at `update auth` are rejected. A key shipped by untrusted update metadata MUST NEVER be used as the release root.

## Non-production RFC test key (CI only)

CI uses the widely published RFC 8032 test public key and a known detached signature fixture solely for fault-injection testing. The signing seed is public knowledge. Build tooling refuses the test key unless `MAYOS_ALLOW_TEST_RELEASE_KEY=1` is **explicitly** set; any such test build reports `TEST KEY match ONLY (not official), install DISABLED`. Never publish it as a real authenticated updater.

## Acceptance and limitations

```sh
make test-ed25519-host
make MAYOS_RELEASE_SIGNATURE=1 MAYOS_ALLOW_TEST_RELEASE_KEY=1 \
  MAYOS_RELEASE_PUBKEY_HEX=d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
  test-ed25519-qemu
```

QEMU boots three isolated MayFS images: known RFC test signature + matching image, tampered signature, and tampered image. The native FFI verifies RustCrypto on x86_64-unknown-none and both tamper scenarios MUST reject.

**Important unfinished security gates:** authenticated TLS with certificate chain, trusted time, CSPRNG; actual GitHub Release streaming download into a bounded store; signature-backed anti-rollback version policy; A/B installation and power-cut recovery, and a full release bootstrap audit. Signature acceptance alone MUST NOT authorize installation.
# Stage 6E: Rust no_std GitHub Release URL and redirect preflight

This is a **pure, bounded URL policy**, NOT a TLS implementation or network client. It cannot establish hostname identity, certificate validity, X.509 trust, reliable DNS, or signed update provenance. Actual HTTPS update requests still remain disabled.

## Scope

The native Rust/C MayKernel gains a Rust `no_std` function `mayrs_release_url_allowed(data, length, redirect_hops)`. It accepts only ASCII HTTPS URLs (max 2048 bytes) with one of the exact hosts below:

- `api.github.com`, restricted to `/repos/ZhongYeMay/MayOS/releases` and descendants.
- `github.com`, restricted to `/ZhongYeMay/MayOS/releases` and descendants.
- `release-assets.githubusercontent.com` and `objects.githubusercontent.com`, but **only** after an already verified HTTPS redirect from a scoped trusted Release endpoint (hop 1–3).

The guard does not accept relative Location redirects, HTTP downgrades, unlisted ports (only omitted or explicit 443), username/userinfo, alternative URL schemes, IPv4 literals, lookalike suffix domains, arbitrary owner/repository changes, embedded credentials, CR/LF/control bytes, backslashes, fragments or path traversal including percent-encoded dot or slash. Its return value represents *eligibility for future authenticated TLS*, not a request authorization.

## Validation

The direct Rust host tests exercise accepted URLs, malicious origin lookalikes, protocol downgrades, path traversal, query escaping and redirect cap; Rust C ABI is exported in a real x86_64 no_std static archive. MayKernel `tls url-test` calls the real Rust function in QEMU, prints a pass/fail diagnostic, and leaves HTTPS entirely disabled.

```sh
rustup toolchain install 1.85.1 --profile minimal --target x86_64-unknown-none
rustup default 1.85.1
make test-rust-host
make clean
make MAYOS_RUST=1 test-rust-url-qemu
```

## Critical security conditions BEFORE actual networking

- The caller MUST establish the server identity through a cryptographically authenticated TLS handshake and check the requested hostname against the certificate.
- Each redirect must be re-validated against the exact allowed host and scheme, with no HTTP downgrade and total hop limit. The guard does not itself parse server HTTP headers or follow redirects.
- The final downloaded bytes must be verified against a separately owner-pinned **Ed25519-signed manifest**, not TLS alone.
- Avoid resolving arbitrary IPs or accepting HTTP proxies/transparent redirects as part of an update.
- Enforce safe storage region and signed update image handling. No install or A/B switching is present.

This is deliberately conservative and does not blindly authorize all GitHub-related hostnames.

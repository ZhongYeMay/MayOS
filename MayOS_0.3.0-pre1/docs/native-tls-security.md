# MayKernel HTTPS update transport: stage 5 security prerequisites

Status: **NOT HTTPS-CAPABLE**. This stage provides real hardware/firmware
observations and a fail-closed policy. It does not contain or emulate a TLS
handshake, certificate verifier, safe client randomness or trusted clock.

## Native components

- `security_entropy.[ch]` checks x86 CPUID for RDRAND, obtains eight hardware
  samples with bounded retries, and rejects trivially unhealthy repeated/all-0
  readings. **It does not export a TLS RNG or form a cryptographic DRBG.**
- `security_clock.[ch]` reads CMOS RTC with update-in-progress and stable
  sample checks, handles BCD/12-hour fields and impossible dates.
  **RTC is always classified UNTRUSTED, regardless of validity.**
- `security_tls.[ch]` rejects update hostnames other than exact
  `api.github.com` or `github.com`, and rejects all HTTPS attempts because
  there is no authenticated TLS provider, cryptographic DRBG, trust store or
  trusted time. Redirect targets will require separate validation later.
- MayShell `tls status` and `tls check api.github.com` display the true
  security readiness and deny unsafe attempts; they never open a socket
  or modify an ISO, bootloader or system partition.

## Acceptance

Run `make test-security` for calendar boundaries, RNG health test data,
hostile hostname rejection, and mandatory HTTPS refusal, with no QEMU or
outside network. The CI compiles all security modules as freestanding ELF64.

Run `make test-tls-qemu` for a genuine MayKernel boot, CPU RDRAND diagnostic,
CMOS observation and MayShell fail-closed status. This test intentionally
verifies HTTPS stays disabled (it is NOT a TLS connection test).

## Integration plan

Select a maintained, reviewed embedded TLS implementation. An initial
candidate is Mbed TLS with platform abstraction, not a handwritten protocol.
Pin a specific source commit, validate source provenance and licenses, and
maintain vulnerability updates. Avoid importing internet-fetched code into
the kernel build without revision pinning.

To enable TLS safely, MayKernel still needs:
1. A tested entropy source plus vetted CSPRNG / DRBG and reseeding.
2. A trustworthy time source for X.509 validity intervals (CMOS alone fails).
3. Correct CA trust anchors and a path for authenticated trust updates.
4. X.509 chain, hostname/SAN, key-usage and certificate-expiration checks.
5. A network I/O abstraction with robust TCP framing, retries and partial I/O.
6. Real TLS handshake and negative tests (expired cert, wrong hostname,
   unknown CA, weak cipher, revoked/compromised signing identities).
7. Separate release artifact publisher signature, image integrity, and
   atomic A/B install/rollback; TLS alone is not software-update trust.

Secure references:
- https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how-do-i-port-mbed-tls-to-a-new-environment-OS/
- https://github.com/Mbed-TLS/mbedtls/releases

Do not mark HTTPS, secure update downloads, or installed system upgrades
as implemented until all of the above have passed native QEMU checks.
## Stage 5B link experiment: optional upstream BearSSL engine

A separate **opt-in native build** is available. It does not enable HTTPS.

- Official source: https://www.bearssl.org/git/BearSSL
- Reproducibility pin: `79c060eea3eea1257797f15ea1608a9a9923aa6f` (older upstream commit, **not** certified/current for production; upgrade/audit before enabling network handshakes).
- Run `git clone https://www.bearssl.org/git/BearSSL vendor/bearssl` and `git -C vendor/bearssl checkout --detach 79c060eea3eea1257797f15ea1608a9a9923aa6f` outside the MayKernel tree.
- Run `make BEARSSL_SOURCE=/absolute/path/to/vendor/bearssl test-bearssl-qemu` from `MayOS_0.3.0-pre1`.
- The build checks the exact Git revision, compiles all upstream portable C sources freestanding with x86_64 no-red-zone settings, links the real TLS engine into MayKernel ELF64, and boots it in QEMU.
- BearSSL engine structures initialize with **zero** trust anchors; TLS 1.2 only is selected for the structural probe; **no handshake occurs**, no certificate validation is claimed and no GitHub network request can be issued.
- A successful CI means **vendor C code compiles/links and QEMU startup stays stable**. The HTTPS security gate remains hard-disabled, even if a provider is linked.
- The default build, including all normal ISO/release tasks, does **not** fetch or include BearSSL and reports it as absent.

**Unresolved issues:** a correctly seeded DRBG and entropy reseeding, trustworthy certificate-validation time, managed CA roots, certificate and SAN verification, a transport socket/stream abstraction, TLS record I/O, robust TCP and negative tests. BearSSL mainly supports TLS 1.2, not TLS 1.3; the pin used here is for portability trials only, not a recommendation to ship obsolete code.

## Verified experimental native link (2026-11-18)

- [Real QEMU workflow SUCCESS](https://github.com/ZhongYeMay/MayOS/actions/runs/44332859250).
- [QEMU screenshot and serial test evidence](https://github.com/ZhongYeMay/MayOS/actions/runs/44332859250/artifacts/14441075104).
- 293 upstream source files compiled freestanding; Unix system RNG backend excluded deliberately.
- Separate native `size_t` wrappers prevent ABI mismatches with MayKernel's legacy `u32` memory helper signatures.
- Source checkout verified against pinned git commit, archive linked as real ELF64. QEMU booted MayKernel and initialized `br_ssl_client_init_full`.
- QEMU explicitly confirmed `PASS: genuine BearSSL native ELF64 init in QEMU; authenticated HTTPS remains blocked`.

**This is NOT a TLS handshake or certificate verification success.** Neither trusted entropy nor trusted wall time nor CA roots have been provisioned. The native provider is opt-in and the default build remains fail-closed. Do not promote the current BearSSL revision as a secure distribution without a new audit and updated vendor source.

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
# Stage 6F — strict HTTP/1.1 response framing for FUTURE TLS Release streaming

## Status

This is a **real Rust no_std, no-allocator parser** in the native MayKernel with a stable C ABI, plus a QEMU command to execute its security test vectors. It is not a network downloader and cannot mark any update authentic or install a system image.

## Frame policy

- HTTP/1.1 only; exactly status 200 for a data response, or 302/307/308 for permitted redirects.
- Response headers are limited to 2048 bytes. Missing terminators are reported as need-more until that bound is reached. Invalid or ambiguous header names and values are rejected.
- Data responses require exactly one bounded `Content-Length`, between 1 and 524288 bytes (matches the current isolated QEMU staging capacity). No gzip, chunked transfer, upgrade, proxy authentication or unknown body-size streaming.
- Redirects require exactly one absolute HTTPS `Location` validated using the pre-existing Rust GitHub Release host and repository allowlist, with a total of at most three redirect hops and `Content-Length: 0`.
- Duplicate `Content-Length`, CRLF injection, ambiguous comma-separated lengths, invalid numbers, wrong owners, HTTP downgrades or unsafe redirects fail closed.

The exported ABI `mayrs_http_headers` returns 1 for a complete accepted header, 0 if it needs more data, and -1 on failure. The C ABI output struct includes status, header size, content length and a bounded redirect target; it does not own an HTTP transport or modify disk.

## Limits

Signed update images must also pass owner-pinned Ed25519 signatures, a complete SHA-256 rehash at the final write boundary, release anti-rollback, trustworthy TLS chain/host/time validation, and bootloader A/B install recovery. These remain unimplemented as a full end-to-end installer. A framing parser or successful test CA certificate chain is never by itself evidence of secure HTTPS network transport.

The MayShell `tls http-test` command executes the actual native Rust selftest but explicitly reports HTTPS remains disabled and never sends packets.
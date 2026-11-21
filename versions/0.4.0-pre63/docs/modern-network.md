# Native modern networking (Linux boot mode)

Explorer now starts the native `/usr/bin/mayos-fetch` ELF directly, without a
shell or browser wrapper. It uses libcurl and the build distribution's TLS
library for HTTP/1.1, HTTPS (TLS 1.2 minimum; TLS 1.3 when negotiated), HTTP/2 via
ALPN, IPv4/IPv6 and DNS. Compressed responses are decoded. Redirects are limited
to five HTTP(S) hops, and HTTPS cannot redirect to plaintext HTTP.

Peer trust, hostname and certificate validity checks are enabled. The default
trust store is `/etc/ssl/certs/ca-certificates.crt`. A deliberately configured
`MAYOS_CA_BUNDLE` replaces it, e.g. for private CAs; there is no insecure mode.
The kernel enables RTC boot-time clock initialization for certificate validity
checks. Accurate firmware time and an up-to-date CA bundle remain necessary.

The UI shows an ASCII text preview capped at 1800 decoded bytes. Requests run
in a separate process with an eight-second deadline and can be cancelled by
Explorer. TLS verification failures are displayed rather than bypassed.

## Boundaries

This applies only to the Linux x86_64 boot mode. MayKernel still lacks a NIC
driver and TCP/IP stack. Wi-Fi, DHCPv6, RDNSS, DoH, HTTP/3/QUIC, a full HTML/JS
engine and general hardware support are not implemented. IPv6 uses the Linux
stack and router advertisements or manual configuration; IPv4 keeps DHCP.
An IP address alone does not establish Internet reachability.

## Build and verify

On Debian/Ubuntu, add `libcurl4-openssl-dev pkg-config ca-certificates` to the
existing ISO build dependencies. `python3-h2` is required only for tests.
`scripts/modern_network.py` builds the worker and packages its actual shared
libraries, ELF interpreter and CA bundle into initramfs. The desktop remains
static musl; the worker is a separate process using the distribution libc.
No build-host shared library path is required inside the guest.

`/usr/share/doc/mayos/network-runtime.json` records capabilities, exact package
and corresponding source versions, and every runtime file's SHA-256. Library
copyright notices are included under `/usr/share/licenses/network-*`. Binary
redistributions must also provide corresponding sources as required by each
library's license; package copyright texts alone are not sufficient. Do not
label an old ISO as containing this implementation.

Tests use local servers, not external Internet availability:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror linux/fetch.c $(pkg-config --cflags --libs libcurl) -o build/mayos-fetch
python3 scripts/modern_network_test.py
python3 scripts/network_runtime_test.py
make dual LINUX_VERSION=7.3.1
python3 scripts/modern_guest_test.py
```

The protocol test exercises actual TLS 1.2/1.3, HTTP/2 ALPN, gzip, IPv6,
untrusted and hostname-mismatched certificates, HTTPS downgrade rejection,
redirect loops and output limits. CI additionally runs the worker in a chroot
containing only packaged files, then inside the actual QEMU guest over e1000
NAT with a dedicated local QA CA.

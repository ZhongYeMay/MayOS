# Native MayKernel TCP fixture bring-up (QEMU only)

This source implements native TCP segment serialization/parsing and a deliberately restricted single-connection diagnostic. It runs over the self-developed RTL8139 driver and the fixed QEMU SLIRP IPv4 interface.

## What is implemented

- IPv4 TCP segment construction: pseudoheader checksum, 20-byte TCP header, flags, sequence and acknowledgement numbers.
- TCP receive decoder: bounded IPv4/TCP lengths, header/checksum verification, IPv4 destination filtering and variable TCP header lengths for incoming SYN-ACK options.
- One guest TCP socket diagnostic using source port 40100, destination 10.0.2.2:10080. Sends SYN, processes valid SYN-ACK and sends ACK.
- Guest sends an HTTP/1.1 GET for `/health` to a host-local QEMU test server. Validates a short `HTTP/1.1 200` response containing `MAYOS-OK`.
- Exposed with `net tcp-probe` and `net status`; tested via `make test-tcp`, `make test-net` and real QEMU serial trace.

## Deliberate limits

- Only localhost testing. Not generalized TCP sockets or a general network client.
- No retransmissions, exponential backoff, RTT estimation, congestion avoidance, TCP receive windows, SACK, IPv6, DNS name resolution or connection pooling.
- **No TLS/HTTPS at all.** Never use this plaintext fixture as a GitHub update transport.
- No signed-manifest authentication, real image download, flashing, A/B rollback or recovery in this stage.
- No production networking claim until retransmission, clean close and adversarial network testing succeed.

## Verification

```sh
make test-tcp            # freestanding TCP packet checksums and rejection tests
make test-net            # real QEMU ARP / UDP / DNS fixture / DHCP / TCP fixture
```

GitHub Actions results must be checked before describing the QEMU TCP exchange as successful. The full safe GitHub update target additionally requires a real DNS resolver, a certified TLS implementation, protected signing keys and failure-safe system installation.
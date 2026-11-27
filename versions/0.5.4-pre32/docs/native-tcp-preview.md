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
## Recovery milestone: bounded retry and clean teardown

The QEMU fixture now uses a distinct ephemeral guest source port for each connection to avoid 4-tuple reuse during TCP TIME_WAIT. The native driver retains exactly one outstanding SYN, data or FIN segment and retransmits it after 100 kernel ticks, up to three times. A rejected/missing acknowledgement does not clear the pending retry. All timer differences use wrapping unsigned arithmetic.

To explicitly exercise packet loss, `net tcp-loss-probe` drops the first outbound SYN *inside the guest before DMA*, then uses the timeout/retransmission path to establish a real TCP connection with the localhost fixture. The QEMU smoke test checks an entirely new handshake, response and FIN close, not earlier serial lines.

`net status` reports recovery count, clean-FIN confirmation and timeout. `make test-tcp-retry` covers ACK validity, retry exhaustion and tick rollover. Actual QEMU acceptance must show the retransmit and a subsequent clean FIN/ACK close; until then this remains development-only.

This is still a single test socket without congestion control, selective ACK, general-purpose TCP or TLS; no production HTTPS or GitHub installer claim.
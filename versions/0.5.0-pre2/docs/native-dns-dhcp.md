# Native DHCP-provided DNS A resolver (experimental)

This is an actual MayKernel IPv4/UDP query to the DHCP-leased DNS server, not the prior test fixture at localhost UDP 10053.

## Implementation
- `net dhcp` acquires an IP address, gateway and DNS server through the RTL8139 adapter.
- `net resolve hostname` builds a DNS A query, sends it over UDP port 53 to the address in the DHCP lease, and accepts responses only from that resolver and expected client UDP port.
- The response must contain the exact original DNS question, transaction ID and a correctly bounded A record.
- Failed/unanswered queries enter an explicit timeout state; the shell never invents an IP address.
- `scripts/net_smoke.py` asks QEMU SLIRP's built-in DNS forwarding service to resolve `localhost` and verifies an actual reply. This test requires QEMU DNS behavior; it is not a public Internet connectivity test.

## Security / production limitations
- DNS replies are unauthenticated. Even matching the question and transaction ID does NOT make DNS responses a trusted software update source.
- No caching, retries, DNS-over-TLS, DNSSEC or public domain guarantee. No persistent DHCP lease or generalized route/ARP management.
- No certificate verification, TLS/HTTPS or safe image installation. This is not a Github Release downloader.

Only mark verified when the native QEMU network test reports a new DNS A response.
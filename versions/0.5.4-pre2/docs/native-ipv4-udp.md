# MayKernel native IPv4 / UDP / DNS-A: bounded preview

This stage builds on the native RTL8139 PCI Ethernet driver and adds packet codecs compiled directly into the freestanding 64-bit MayKernel.

## Implemented

- IPv4 header serialization with version, total length, non-fragmenting DF flag, IPv4 header checksum.
- UDP serialization with pseudoheader checksum and minimum Ethernet frame padding.
- Receive validation for local MAC, destination IP, IPv4 total length and checksum, UDP size and checksum; fragmented datagrams and IPv4 options are rejected.
- Strict DNS-A query encoder and bounded A-record reply parser, with rejection tests for malformed labels, truncated records, invalid transaction ID, or DNS error responses.
- MayShell commands: `net status`, `net qemu-probe`, `net udp-probe`, and `net dns-probe`.
- The QEMU test uses the isolated 10.0.2.0/24 SLIRP network and a Python test server bound to host localhost UDP port 10053. It validates an IPv4/UDP echo and a **synthetic** DNS A record for example.test (203.0.113.7).

## How to test

```sh
make test-net-codec   # pure C tests; no QEMU
make test-net         # native ISO + actual QEMU RTL8139 ARP, UDP and DNS fixture
make test             # full MayKernel BIOS/QEMU and GUI regression suite
```

QEMU requires `-nic user,model=rtl8139` for the network smoke. The script starts and closes its own localhost fixture; do not treat the fixture as an internet DNS resolver.

Expected serial messages if real guests succeed:

```text
MayOS: RTL8139 ARP reply received from QEMU gateway 10.0.2.2
MayOS: RTL8139 IPv4 UDP echo validated from QEMU host
MayOS: RTL8139 DNS fixture response validated over IPv4 UDP
```

## Not implemented

- Configurable IPv4 addresses or route tables; only hardcoded QEMU 10.0.2.15 / gateway 10.0.2.2 for diagnostics.
- DHCP lease negotiation, actual QEMU DNS server requests, DNS caching, TCP, TLS or HTTPS.
- Public internet connectivity, GitHub Release downloads, authenticated update packages, installation or rollback.
- Real-hardware DMA/IOMMU and malicious-LAN security guarantees.

This development branch must not be published as a full updater or production networking release.

Next priority: DHCP (DISCOVER/OFFER/REQUEST/ACK) with state validation and lease renewal, UDP routing and ARP cache, native DNS service 10.0.2.3, then TCP/TLS and signed image update transport.
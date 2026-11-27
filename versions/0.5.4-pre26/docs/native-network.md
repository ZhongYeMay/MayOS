# MayKernel Native Ethernet: RTL8139 link-layer preview

This stage implements a **native 64-bit MayKernel PCI RTL8139 polling driver**. It does not use a Linux driver or simulated web page. It does NOT enable Internet or GitHub downloads.

## Source and behavior
- kernel/net.c and net.h: PCI configuration scan for Realtek 10ec:8139. Enable I/O+DMA only for matching devices.
- Reset, read hardware MAC, configure 8 KiB RX ring with wrap padding and four bounded TX buffers; accept own MAC and broadcast only, not promiscuous mode.
- Receive and validate Ethernet/ARP reply packets, with packet counters and bounded polling.
- MayShell: net status and net qemu-probe (QEMU-only ARP diagnostic).
- scripts/net_smoke.py: boot the real MayKernel ISO in QEMU with -nic user,model=rtl8139, send the probe from 10.0.2.15 to 10.0.2.2, confirm the complete reply in guest serial logs and capture framebuffer screenshot.

## Build and acceptance
- make: build native ELF64 MayKernel and ISO.
- make test-net: real QEMU Ethernet ARP TX/RX smoke; make test adds this to the existing regression suite.
- A PCI detection or transmit-only success is NOT proof of networking. The smoke explicitly waits for the gateway reply.
- Actual pass/fail status must be read from GitHub Actions; unverified work is not a release.

## Unsupported / limits
- No real-hardware certification, PCI hotplug, MSI/interrupt path, DMA isolation, or link recovery yet.
- No DHCP, general IPv4, UDP, TCP, DNS, IPv6, TLS, HTTPS, HTTP/2, or GitHub Release requests.
- No downloading, writing OS images, authenticated installation or rollback.
- Next: hardened RX behavior -> ARP and IPv4/UDP -> DHCP/DNS -> TCP -> verified TLS/HTTPS -> full-image authentication -> safe rollback-capable installer.
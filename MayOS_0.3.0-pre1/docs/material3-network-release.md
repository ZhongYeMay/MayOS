# MayOS 0.3.0 — Material 3 desktop and basic networking

This document describes the intended stable-release verification scope. A commit
on `main` is not itself proof that a stable ISO has been published.

## Desktop design
- Shared 1024×768 GUI for MayKernel (x86) and Linux (x86_64).
- Material Design 3-inspired roles: primary, on-primary, surface, surface-high,
  selected container and outline; separate dark and light tones.
- Rounded elevated windows, tonal selection states, filled buttons, drawer,
  navigation rail and programmatically drawn outline icons.
- No runtime dependency on Material icon fonts or Google online services.
- Existing file, editor, shell, settings and Explorer workflows remain available.

## Networking
**Linux boot mode only**: an emulated e1000 Ethernet NIC, DHCP lease renewal,
DNS configuration, IPv4 diagnostics (`net` / `network` in MayShell),
and the Explorer `http://` text-response preview.

Serial Linux shell also exposes BusyBox `ip addr`, `ip route`, `ping`,
and `nslookup`. IPv4 diagnostics read the guest's actual interface state,
default route, and resolver. These are not mock connection indicators.

HTTPS/TLS, a full HTML browser, Wi-Fi setup, network shares, custom-kernel
TCP/IP, and hardware drivers outside the QEMU validation matrix are **not**
implemented. HTTP support is plaintext and should not be used for passwords
or other sensitive information.

## Gate before public stable release
The automated release workflow must:
1. Compile and test the custom MayKernel ISO with live QEMU framebuffer/input.
2. Build the current dual-boot ISO with Linux 7.3.1.
3. Verify BIOS and UEFI boot paths, Linux GUI, and disk isolation.
4. Verify QEMU e1000 DHCP-assigned IPv4, default route, and DNS, without
   requiring public Internet access.
5. Produce SHA-256 checksums for the exact downloadable ISO files.
6. Publish the GitHub stable tag and Release **only after all prior steps pass**.

Historical Releases must not be deleted. Download mirrors and Pages updates
should never imply that a release exists until the GitHub Release is visible.

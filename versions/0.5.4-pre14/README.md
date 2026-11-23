# MayOS 0.5.4-pre14 · shared desktop and picture Easter egg

**MayKernel64 prerelease.** The hybrid ISO provides BIOS and x86_64 UEFI boot with Secure Boot disabled.

## What changed

The Linux desktop previously kept files only in process RAM. This version supports opt-in persistence to a previously prepared, separate ext4 data volume. It includes the application launcher, working menus and Settings from MayOS 0.5.3-pre2.

- Default: RAM-only session, no probing or writing arbitrary drives.
- Persistence requires an explicit Linux kernel parameter such as `mayos.persist=/dev/sda`, a real ext4 filesystem, and a root marker file `.mayos-profile-enabled` containing exactly `MayOS-Profile-v1` plus a newline.
- `mayos.ram` or `mayos.live=1` always forces RAM-only even if a persistence parameter is present.
- The init script first mounts the specifically selected device read-only, verifies its marker, then remounts it read/write. It never formats disks.
- The desktop uses exclusively the marked mount directory, never raw disk I/O. Its two A/B snapshot files include CRC32 validation, atomic rename and file+directory fsync.
- If the newest snapshot is corrupt, the previous valid snapshot is restored. If both are corrupt, persistent writes are refused to protect remaining evidence.
- The existing limits remain: 48 file slots and <4096 bytes per file. It is not a full POSIX filesystem, package manager or app sandbox.

## Safe QEMU demonstration on a disposable Linux-host image

Run these commands **only in a temporary development folder**; never use a physical disk containing data.

```sh
mkdir -p /tmp/mayos-profile-seed
printf 'MayOS-Profile-v1\n' > /tmp/mayos-profile-seed/.mayos-profile-enabled
truncate -s 128M mayos-profile-test.img
mkfs.ext4 -F -d /tmp/mayos-profile-seed mayos-profile-test.img
```

Attach `mayos-profile-test.img` as a separate virtio test disk:

```sh
qemu-system-x86_64 -m 1024 -vga std -cdrom build/mayos-dual.iso -boot d \
  -drive file=mayos-profile-test.img,format=raw,if=virtio
```

In GRUB select **Linux64 desktop**, press `e`, append `mayos.persist=/dev/sda` to the Linux kernel line and boot. On success the serial log includes `MayOS Linux profile: explicit marked ext4 mounted for atomic desktop snapshots`. Save a text document and reboot using the same disk and the same explicit boot parameter. The serial log should report `MayOS Linux: persistent desktop profile restored` and the document must remain present.

If no marker or volume is present, the desktop stays temporary in RAM. The boot argument is intentionally required on every boot; no automatic disk selection is implemented.

## Local host tests

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -DMAYOS_LINUX \
  scripts/linux_profile_test.c linux/profile_persist.c -o /tmp/mayos-profile-test
/tmp/mayos-profile-test --ram
/tmp/mayos-profile-test --badmarker
/tmp/mayos-profile-test --snapshot
```

## Real QEMU reboot and abrupt power-loss evidence

The Linux64 x86_64 desktop has been tested with a **disposable 128 MiB ext4 QEMU image**, including an actual file created by the GUI and read back after a separate Linux reboot. The follow-up [power-cut regression](https://github.com/ZhongYeMay/MayOS/actions/runs/44993793801) kills the QEMU process immediately after the GUI reports a completed, fsynced snapshot. A subsequent cold boot reads the saved file successfully. Another disposable ext4 image without the explicit marker remains byte-identical after both normal shutdown and an abrupt QEMU kill.

Source: `scripts/linux_persistence_qemu_smoke.py`; reproducible workflow: `.github/workflows/linux-persistence-powercut.yml`. The workflow reuses a SHA-256 verified Linux ISO when available and rebuilds from the local development sources after artifact expiration.

**Boundaries:** This verifies one abrupt termination point **after a completed fsync**, not arbitrary interruption during every phase of filesystem writes or disk cache flush. A failed storage device, a full filesystem, partial writes, actual power loss on physical hardware, user isolation, large files, app packages, a browser, and broad hardware driver compatibility remain future work. The system is still a *0.5.4-dev preview*, not an upgrade to the published v0.5.2 ISO.


## Bing daily wallpaper

On Linux64, new desktop settings select Bing by default. Personalization → Desktop
can be used to select Bing on an existing profile or to choose the built-in
backgrounds. A separate HTTPS worker checks the Bing US daily feed every hour,
or retries after five minutes if it cannot connect. It downloads an image only
when its published date changes. Disconnecting keeps the last image until the
end of the current session. Offline starts and failed first downloads use the
built-in background. Build the Linux worker with the libcurl, libjpeg and
json-c development packages.

MayKernel64 uses DHCPv4, DNS and native TLS 1.2 to request the Bing US daily
feed. With signed UTC active and Bing daily selected in Settings, it checks
hourly and fetches the image only when the feed publishes a newer date. A
failed request is retried after five minutes. The desktop keeps the last
successfully decoded image until reboot and can start from a GRUB module
tagged `mayos-wallpaper`. ISO release packages contain no Bing photos.

The public MayKernel64 ISO automatically attempts signed time synchronization
after acquiring DHCP. It requires supported Ethernet, working RDSEED, DNS and access
to the Roughtime and Bing services. Until it can obtain authenticated time,
the desktop uses its most recent image or the built-in background.

The local regression command is
`python3 scripts/bing_wallpaper_qemu_smoke.py --bearssl-source /path/to/pinned/BearSSL`.
It exercises isolated UDP Roughtime and HTTPS origins with test-only keys.
It checks automatic clock bootstrap and renewal, rejection of an altered signature
and a replay, and the wallpaper workflow without manual time entry. The production build always uses the built-in Mozilla roots.
The native build vendors [stb_image 2.34](https://github.com/nothings/stb/blob/6b5799bae8a1fe3792691ef4b72ae26434c3e799/stb_image.h)
for JPEG and [jsmn](https://github.com/zserge/jsmn/blob/25647e692c7906b96ffd2b05ca54c097948e879c/jsmn.h)
for JSON.

## MayKernel64 interaction and networking

MayKernel64 automatically acquires an IPv4 address over supported E1000 and
RTL8139 adapters. Missing responses are retried with backoff. After an expired
lease the kernel starts fresh discovery. The Network settings panel reports the
leased address. Use the left or right Super key on PS/2 or USB boot keyboards
to open the application launcher.

DHCP-resolved DNS now selects an on-link resolver or the router as its ARP
next hop, using the leased IPv4 address. MayKernel replies to ARP for its
leased address. The network development regression exercises both adapters on
a custom SLIRP subnet using `scripts/dhcp_routed_dns_smoke.py`.

## Native IPv4 TCP client

The MayKernel preview can open one outbound TCP stream from its DHCP lease.
It resolves the Ethernet next hop with ARP, supports bounded retransmission
and timeouts, and exchanges data through a nonblocking read/write interface.
MayShell's `net tcp-echo <IPv4> <port>` checks an 8192-byte echo round trip
against a server you control. `net status` reports the current stream state.
The QEMU smoke test verifies data in both directions, graceful close and
the Ethernet next hop for an off-subnet TCP SYN.

This preview uses one in-flight transmit segment and IPv4 only.

## Native TLS transport

The optional pinned BearSSL build includes a nonblocking TLS 1.2 adapter over
MayKernel's outbound TCP stream. The public TLS entry point uses the built-in Mozilla certificate anchors and requires signed UTC. BearSSL obtains fresh entropy from CPU RDSEED. The adapter
uses hostname verification, ECDHE with AEAD suites, and distinguishes authenticated close from a bare
TCP EOF. The kernel builds BearSSL with scalar AES and ChaCha implementations
because the early MayKernel runtime does not save or restore XMM state.

For the isolated end-to-end test, run
`python3 scripts/tls_wire_smoke.py --bearssl-source /path/to/pinned/BearSSL`.
It creates a private QEMU image and a disposable local TLS server. The public
image has no fixture CA or test seed. The tests cover the public trust store
rejecting the private test CA, as well as private TLS with an isolated anchor.
The system update channel is not enabled by this release.

MayKernel samples RDSEED for every new BearSSL client context. Sampling has a
bounded retry budget and rejects duplicate words and stuck samples. The seed
is wiped after mixing into HMAC-DRBG. RDRAND, the RTC and uptime are unavailable
as fallback sources. On systems without working RDSEED, the TLS adapter
refuses to open TCP unless the isolated diagnostic build provides its own
test seed. The tests exercise both CPU feature configurations in QEMU.

## Automatic signed network time

The public ISO uses Cloudflare's published Ed25519 root for
`roughtime.cloudflare.com:2003/UDP`, with IETF draft 11 and 08. The client
requires a fresh RDSEED challenge, checks the nonce proof, verifies both signatures,
and rejects excessive time uncertainty or rollback. It synchronizes after DHCP,
tries again every 30 seconds on failure, and refreshes every hour after success.
A signed clock expires after six hours without a successful refresh. Nothing
written by an unauthenticated NTP server, the RTC or DNS is used as a trust
anchor. A service root rotation requires a MayOS build update.
[Cloudflare Roughtime service and public key](https://developers.cloudflare.com/time-services/roughtime/usage/)

```sh
make all MAYOS_ROUGHTIME=1 BEARSSL_SOURCE=/path/to/pinned/BearSSL
```

Use `tls status` in MayShell to check whether signed UTC is active.

## Public CA trust and manual signed UTC

The BearSSL build embeds 118 roots from the [Mozilla CA bundle extracted by curl](https://curl.se/docs/caextract.html).
The input is pinned by SHA-256 in the source tree. Generating the trust
anchors and signing clock credentials requires Python with cryptography 40
or later.

With a trustworthy UTC clock, an operator can create an Ed25519 time authority
and build an ISO with only its public key. Run the key generation on the
authority and keep the private key there:

```sh
umask 077
openssl genpkey -algorithm ED25519 -out mayos-clock.pem
openssl pkey -in mayos-clock.pem -pubout -outform DER | tail -c 32 | xxd -p -c 32
```

Set `MAYOS_AUTHORITY_PUBLIC_HEX` to the 64 hexadecimal characters printed by the second command and build:

```sh
make all MAYOS_RUST=1 BEARSSL_SOURCE=/path/to/pinned/BearSSL \
  MAYOS_TIME_PUBKEY_HEX="$MAYOS_AUTHORITY_PUBLIC_HEX"
```

In MayShell, run `tls time-challenge`. On the clock authority, run
`python3 scripts/time_credential.py --key mayos-clock.pem <challenge>`
and enter its output in MayShell within two minutes. Use `tls status` to
check the result. Signed UTC advances using the kernel timer for up to six
hours. Repeat enrollment after expiration or reboot. The public ISO uses automatic Roughtime. The commands in this section are for
a separately built image that trusts an operator-controlled manual authority.

## Desktop and boot appearance

The boot menu, startup status, installer, first-run setup and desktop use the
same dark palette. Fresh profiles use a white Fluent-style arrow with a dark
outline. The dark and imported pointer options are available in Personalization.

The arrow is adapted from [Microsoft Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons/blob/a3de63245b4fa49c30e20d9838593a105affe774/assets/Cursor/SVG/ic_fluent_cursor_24_filled.svg).
The original SVG and MIT license are in the source tree. The ISO includes the license.

MayShell’s hidden `angelkawaii` command opens the bundled picture titled “超絶最かわてんしちゃん”. Press Esc to close it.

Physical-machine acceptance targets x86_64 systems across vendors. The
initial matrix covers UEFI and legacy BIOS where available, SATA/AHCI and
NVMe storage, USB and PS/2 input, and common display modes. QEMU covers
the virtual firmware and disk combinations exercised by the installer
tests. Physical results should record CPU, firmware mode, storage and
input controller rather than infer compatibility from the computer brand.

## Installed systems with SATA and NVMe present

New installations pass the installer-generated GPT disk GUID to MayKernel
from GRUB. The kernel compares the identifier and checks the MayFS
partition before writing. If the disk is absent or the identifier is visible
on multiple probed backends, the session uses RAM. Existing boot entries
without the selector still use legacy storage discovery. Reinstalling is
unnecessary to use legacy entries. Host-side image upgrade and rollback
preserve the existing GPT disk GUID when regenerating the boot entries.

The current MayKernel drivers expose one device per supported storage
backend. The multi-disk regression covers one SATA/AHCI disk and one NVMe
disk with both attached, including installation, two cold starts without
the ISO, decoy-disk checksums, and duplicate GPT identifiers.

```sh
python3 scripts/installed_storage_smoke.py \\
  --ovmf-code /usr/share/OVMF/OVMF_CODE_4M.fd \\
  --ovmf-vars /usr/share/OVMF/OVMF_VARS_4M.fd
```

## Large installer targets

The standalone installer accepts SATA/AHCI and NVMe disks with 512-byte
logical sectors beyond the old 128 GiB capacity limit. It relocates the backup
GPT to the final LBA and verifies the written layout. The protective MBR field
saturates beyond 2 TiB. The installer still scans the entire target for existing
data before writing. The partition sizes in the installation image are fixed.

Run `make test-installer-geometry` for GPT layout checks. The QEMU test
`scripts/large_disk_smoke.py` covers the nonblank guard on a 512 GiB NVMe
target and cold boot on an installed 3 TiB sparse disk. Its optional
`--full-install` argument also performs a full scan and installation
on a blank 129 GiB virtual NVMe target.

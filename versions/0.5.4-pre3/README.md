# MayOS 0.5.4-pre3 · Desktop integration preview

**Development source only.** The released v0.5.2 and previous v0.5.3-dev directories remain unchanged. This branch does not claim a new stable release.

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

MayKernel64 can display a prepared local snapshot from a GRUB module on the
boot media tagged `mayos-wallpaper`. Direct recurring HTTPS downloads are
currently available only on Linux64. ISO release packages contain no Bing photos.

## MayKernel64 interaction and networking

MayKernel64 automatically acquires an IPv4 address over supported E1000 and
RTL8139 adapters. Missing responses are retried with backoff. After an expired
lease the kernel starts fresh discovery. The Network settings panel reports the
leased address. Use the left or right Super key on PS/2 or USB boot keyboards
to open the application launcher. This update does not implement public
Internet TCP or native HTTPS.

DHCP-resolved DNS now selects an on-link resolver or the router as its ARP
next hop, using the leased IPv4 address. MayKernel replies to ARP for its
leased address. The network development regression exercises both adapters on
a custom SLIRP subnet using `scripts/dhcp_routed_dns_smoke.py`.

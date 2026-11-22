# MayOS 0.5.4-dev · Linux64-first persistent desktop

**Development source only.** The released v0.5.2 and previous v0.5.3-dev directories remain unchanged. This branch does not claim a new stable release.

## What changed

The Linux desktop previously kept files only in process RAM. This version supports opt-in persistence to a previously prepared, separate ext4 data volume. The existing desktop design is unchanged.

- Default: RAM-only session, no probing or writing arbitrary drives.
- Persistence requires an explicit Linux kernel parameter such as `mayos.persist=/dev/vda`, a real ext4 filesystem, and a root marker file `.mayos-profile-enabled` containing exactly `MayOS-Profile-v1` plus a newline.
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

In GRUB select **Linux64 desktop**, press `e`, append `mayos.persist=/dev/vda` to the Linux kernel line and boot. On success the serial log includes `MayOS Linux profile: explicit marked ext4 mounted for atomic desktop snapshots`. Save a text document and reboot using the same disk and the same explicit boot parameter. The serial log should report `MayOS Linux: persistent desktop profile restored` and the document must remain present.

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


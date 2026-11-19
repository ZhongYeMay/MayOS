# MayKernel durable A/B journal: test-only ATA persistence milestone

This stage implements **actual QEMU ATA sector I/O** for a Rust `no_std` recovery journal, deliberately limited to isolated diagnostic disks. It is **NOT** an installer and does **NOT** select a boot slot.

## Safety boundary

- The diagnostic is absent from ordinary MayOS builds. It is compiled ONLY with `MAYOS_AB_DISK_DIAGNOSTIC=1` and a Rust `ab-disk-diagnostic` feature. Default builds contain no journal-writing ABI.
- It refuses every disk except an **exactly 8 MiB** primary IDE master with a valid MayFS directory file `Documents/AB-DIAGNOSTIC.marker` containing the exact bytes `ISOLATED-QEMU-AB-JOURNAL-ONLY`.
- It reserves LBA **1024 and 1025**, beyond both existing MayFS snapshots at LBAs 0–1023. If either reserved LBA contains nonzero non-MayOS content, it refuses ALL operations.
- The test only initializes a 48-byte journal in the first blank record, then writes a **synthetic trial** in the second previously blank record; it never replaces a valid copy, never updates a bootloader, never installs an image, and cannot represent an authenticated release. The synthetic authentication parameter exists only in the diagnostic feature.
- Each ATA sector write is followed by `ATA FLUSH CACHE` and complete readback comparison. The latest generation is selected by Rust after checking format, field constraints and per-record FNV checksum. FNV detects accidental corruption, **not** attacker forgery.

## Tests

```sh
rustup toolchain install 1.85.1 --profile minimal --target x86_64-unknown-none
rustup default 1.85.1
make test-rust-host
make clean
make MAYOS_AB_DISK_DIAGNOSTIC=1 test-ab-journal-qemu
```

The real QEMU test creates isolated 8 MiB MayFS disks. It boots the genuine native MayKernel repeatedly:

1. Writes a generation-1 journal in the first record, with disk flush and readback.
2. Cold-boots and recovers generation 1.
3. Writes generation-2 synthetic trial in the second record and flushes.
4. Cold-boots and selects generation 2.
5. Powers the VM off, intentionally corrupts generation-2's checksum (a simulated torn/corrupt write), cold-boots and falls back to generation 1.
6. Asserts both original MayFS snapshots are **byte-for-byte unchanged**.
7. Asserts an unrecognized reserved-sector owner and an unmarked disk remain byte-for-byte unchanged after rejected commands.

This test proves mirror-selection and persistence under **QEMU flush/reboot and host-injected corruption**. It does **not** model a real physical power interruption within a sector write or a complete on-disk A/B update system.

## Missing before production

- An independently partitioned boot volume with two verified bootable system images.
- A bootloader-readable journal format with atomic state updates and fsync-like durability barriers tested on actual storage.
- Bootloader integration (GRUB/Limine/UEFI) that decrements a trial counter *before* launching the new slot.
- An installer constrained to the inactive partition with signed-image verification at write/commit boundaries.
- Independent health confirmation and monotonic anti-rollback anchored outside attacker-controlled storage.
- Power-cut injection between every physical write and flush, plus recovery/failure test matrices.
- Trusted TLS/HTTPS and authenticated large-image Release download.

Until these gates pass, the user-facing Updates page and `update auth` must not offer installation.

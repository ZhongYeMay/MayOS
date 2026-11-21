# MayKernel Rust no_std A/B boot rollback policy — diagnostic milestone

## Live ISO upgrade on the QEMU host

`scripts/live_upgrade.py` manages two bootable ISO files and a fixed, separate
MayFS data image. It targets the published x86_64 MayKernel Live ISOs with QEMU
BIOS. Close the VM and back up the existing MayFS image before enrolling it.
Use the SHA-256 values from the corresponding release's trusted checksums.

```sh
python3 scripts/live_upgrade.py --directory ./boot-slots init \
  --disk /absolute/path/to/mayos-disk.img --iso ./pre1.iso \
  --version 0.5.0-pre1 --sha256 "$PRE1_SHA256"
python3 scripts/live_upgrade.py --directory ./boot-slots upgrade \
  --iso ./pre2.iso --version 0.5.0-pre2 --sha256 "$PRE2_SHA256"
python3 scripts/live_upgrade.py --directory ./boot-slots run
```

For signed releases, pass `--manifest Update.may --signature Update.sig`
and `--trusted-public-key /separate/path/MayOS-public.pem` to `upgrade`
in place of `--version` and `--sha256`. This requires OpenSSL 3.

The trial uses a temporary disk overlay and verifies the version banner,
the MayFS mount and desktop readiness over serial. The old ISO stays active
if the trial fails. After success, `run` boots the selected ISO with the
original MayFS image. Use `status` to inspect the selection and `rollback`
to select the previous verified ISO. Rollback keeps the current MayFS data,
including files saved after upgrading.

Settings-based updates and installed-PC boot slots require a separate
bootloader contract. See the MayKernel policy and diagnostics below.

## MayOS 0.5.1 installed-disk development preview

`scripts/install_mayos.py` builds a GPT/UEFI virtual hard disk containing
GRUB, two kernel slots and a MayFS partition. Use Linux with `sfdisk`,
`xorriso`, GRUB EFI utilities and mtools. Upgrade trials additionally use
QEMU and OVMF. Shut down the VM before using any of its disk images.

```sh
# Fresh installation. Optionally add --import-mayfs /path/to/mayos-disk.img
# to migrate the two snapshots from an existing legacy MayFS image.
python3 scripts/install_mayos.py --iso base.iso --sha256 "$BASE_SHA256" \
  --output installed.img

# Build a separate upgraded image. Trial boot uses a temporary overlay.
python3 scripts/install_mayos.py --upgrade-from installed.img --iso newer.iso \
  --sha256 "$NEW_SHA256" --version 0.5.1-pre2 --output upgraded.img

# Bring the preceding kernel slot back while keeping current MayFS files.
python3 scripts/install_mayos.py --rollback-from upgraded.img --output rolled-back.img
```

Specify a new output filename for each operation. Use a MayKernel ISO built
with the new GPT partition support. Boot the resulting disk as an x86_64
UEFI hard drive. An upgraded disk offers both slots in GRUB. A failed
trial leaves no output image and the input image stays byte-for-byte intact.
The host tool writes virtual disk images.

### Bootable installation media

Build MayKernel with `make`, then supply a Linux x86_64 boot kernel and
its matching installed module tree to `scripts/build_installer_iso.py`.
The builder also needs static BusyBox, GCC with static libc, GRUB and the
partitioning tools listed above. It embeds the checked MayKernel image
and all runtime payloads in the installer ISO.

```sh
python3 scripts/build_installer_iso.py --iso build/mayos.iso \
  --sha256 "$MAYOS_SHA256" --kernel "$LINUX_KERNEL" --kernel-version "$LINUX_VERSION"
```

Boot `build/mayos-installer.iso` and select the console installer. Enter a
listed disk path, then type the full confirmation shown on screen. The
installer takes the target exclusively and checks that every byte is
zero before writing. Once readback passes, remove the ISO and start the
installed disk in UEFI mode. Use a dedicated empty virtual disk at least
80 MiB in size with 512-byte logical sectors. Keep the original MayFS
disk separate when using the host migration procedure.

The installer medium currently handles fresh installs. Existing-disk
upgrades and rollback use the host commands above. Physical targets and
BIOS hard-disk installation still need validation.

## Actual implementation

A real Rust no_std state machine (`rust/maykernel_policy/src/ab_policy.rs`) defines a bounded, C ABI-compatible 48-byte boot-journal record. It models A/B active and trial slots, a trusted minimum security epoch, a monotonically incremented journal generation, a three-boot trial limit, and success/rollback transitions. It never allocates memory or calls a kernel service.

The policy rejects staging the currently active slot, unauthenticated candidate requests, candidate epochs not newer than the trusted minimum, out-of-range slots, corrupted records, ambiguous records at the same generation, and counter overflows. An attempted boot consumes a trial before it occurs. Only explicit positive health confirmation commits the new slot and advances the epoch. After exhausting trials, the previously confirmed active slot is preserved. The policy recovers the newer of two independently checksummed journal copies when one is missing/corrupt.

## Verification

- Unit tests: unsigned or downgraded staging rejected; trial budget bounded; health-confirmation gated; corrupted and torn mirrors rejected; sequence overflow fails closed.
- Native MayKernel + C ABI QEMU: MayShell `update recovery-test` runs a real in-kernel diagnostic. The serial log prints `MayOS: Rust A/B recovery policy self-test PASSED` only when the complete state transition exercise passes.
- The same unit module compiles with direct rustc (legacy Rust-only build) and the optional Cargo Ed25519 signature build.

## Not implemented yet — safety-critical

**This is a policy engine, not a usable A/B updater.** It does NOT:

- Write a boot journal to real persistent partitions with power-cut atomicity and durable flush verification.
- Select a GRUB/Limine boot entry or change actual EFI boot variables.
- Create or validate a system image partition, download a Release, or write to a disk.
- Verify a future installer has really supplied signed-authenticated state; the policy's `authenticated` input is currently only exercised by trusted tests.
- Verify a monotonic anti-rollback counter stored in TPM/UEFI NVRAM.
- Mark a boot successful based on real post-boot health, or complete power-cut test matrices.

MayFS already has dual checksummed snapshots for ordinary files, but this is **not** the same thing as a bootable system A/B installation. The future boot journal must be readable by the bootloader and resilient to power cuts. A future installer must re-verify publisher signatures and complete image digests at the write boundary. Do not claim that `update recovery-test` performs an installation.

## Suggested next gates

1. Define and partition two independent bootable images; prevent writes outside explicitly selected inactive slot.
2. Persist two journal copies with sequence numbers and device flushes, then inject power loss at every transition.
3. Implement bootloader pending-slot selection and decrement-before-boot.
4. Confirm only after signed image, hardware compatibility, filesystem and essential services pass health probes.
5. Verify rollback on boot timeout and repeated failure, and enforce monotonic authenticated release epochs.

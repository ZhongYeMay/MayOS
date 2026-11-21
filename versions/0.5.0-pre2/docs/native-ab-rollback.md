# MayKernel Rust no_std A/B boot rollback policy — diagnostic milestone

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
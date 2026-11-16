# MayOS System Update: transactional boot-record design (developer draft)

This is a **design and in-memory test model**, not a working installer.

The current MayOS BIOS/UEFI boots from a read-only ISO. Its 8 MiB MayFS
data disk is **not** a 24–42 MiB system update slot. The native MayKernel
does not yet have HTTPS/TLS or a safe block-device partition updater.
Never write an ISO over the current MayFS disk or active system storage.

## Proposed safe end-to-end path (must pass QEMU before 0.6 stable)

1. Transport: native MayKernel network stack with real DNS, TCP and
   validated TLS; no HTTP downgrade and no redirect outside an approved
   download host.
2. Metadata: authenticated release catalog with exact target name,
   version, x86_64 architecture, compatible kernel, ISO size and SHA-256.
3. Download: count actual bytes only; persist to a **dedicated, bounded**
   inactive staging area; verify size and SHA-256 of the persisted image,
   not just transient stream bytes.
4. Image format: a **signed installable MayOS payload** with a manifest
   describing required bootloader format and partition bounds. Existing
   GitHub ISO assets are **not** installable upgrade payloads.
5. Atomic metadata: provision two known-good boot slots A/B. Write only
   the inactive slot and commit redundant checksummed boot records after
   all writes have been verified and synchronized.
6. Reboot: bootloader reads the newest valid record. Decrement retry
   attempts durably before trying the pending slot. If no health-check
   confirmation is received, return to the previous confirmed slot.
7. Health confirmation: current kernel must verify actual running slot
   and filesystem integrity before marking the new slot confirmed.
8. Failure injection: test every power-cut checkpoint under QEMU, plus
   truncated files, wrong digest, stale metadata, unexpected slot
   boundaries, and two consecutive failed boots.

\`kernel/update_bootplan.[ch]\` implements **only** deterministic,
freestanding selection logic with CRC-32 checksummed redundant metadata.
It does not operate on a disk, network, bootloader, GPT/MBR or restart.

\`kernel/update_sha256.[ch]\` and \`kernel/updater.[ch]\` supply verified
streaming digests and release selection. They do **not** persist a staged
image. Never show an installation percentage until a real installer
reports actual committed bytes and successful boot record updates.

Stable 0.6.0 Release remains BLOCKED until the complete path passes real
end-to-end validation. Never call QEMU unit tests proof of physical-PC
installer safety.

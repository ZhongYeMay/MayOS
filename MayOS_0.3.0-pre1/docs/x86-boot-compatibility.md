# x86 / x86_64 boot compatibility scope

This phase changes the boot path and standard firmware framebuffer handling.
Device-driver expansion is deferred. It does not promise every PC is supported.
The internal desktop remains 1024x768; it is scaled to the firmware framebuffer
with preserved aspect ratio. Supported firmware RGB formats are RGB565 and
24/32-bit layouts with non-overlapping color fields, at 640x480 or larger.
Both backends validate pitch, offsets and accessible framebuffer bounds.

## Intended boot matrix

| CPU | Firmware | MayKernel | Linux desktop / rescue |
| --- | --- | --- | --- |
| x86 i686 with CPUID | BIOS | 32-bit custom kernel | unavailable: Linux image is x86_64 |
| x86_64 | BIOS | 32-bit custom kernel | x86_64 |
| x86_64 | UEFI x64, Secure Boot off | 32-bit custom kernel via GRUB | x86_64 |

GRUB checks long-mode CPU support before exposing Linux entries. It tries
1024x768, 800x600, 640x480 and the firmware default graphics mode. Linux retains
the firmware mode rather than forcing efifb to one resolution. The separate
compatibility rescue entry disables SMP/APIC/MSI for diagnosis; it is not the
normal boot configuration or a substitute for device drivers.

## Low-resolution MayKernel recovery (BIOS and x64 UEFI)

Both the standalone MayKernel ISO and the dual-kernel ISO now provide a final
GRUB entry, **MayOS - custom kernel (safe graphics)**. Choose it with the End
key (or arrow keys), then Enter, if the default MayKernel desktop remains
blank or the firmware rejects its preferred video mode.

The safe entry attempts **640x480x32**, then **800x600x16**, then the firmware's
automatic mode. It retains the exact same native MayKernel and MayFS disk
behavior; this is a **firmware RGB mode fallback**, not a universal GPU driver.
On machines whose firmware cannot expose an accessible RGB framebuffer below
4 GiB, MayKernel may still fail; Linux rescue over serial can help diagnose
the problem on 64-bit CPUs.

The standalone ISO also exposes the GRUB menu on a standard 115200 baud serial
port, matching the dual-kernel ISO. This enables menu selection and diagnostics
when the firmware video console is unreliable. No physical machine support is
claimed solely from QEMU checks.

The boot compatibility CI now starts **both real ISOs** on 32-bit BIOS and x64
OVMF, selects the safe entry rather than the default, and verifies the actual
low-resolution framebuffer, desktop screenshot and keyboard. Its mode-matrix
builder restores the original standalone ISO after changing temporary
resolution settings so a prior test cannot contaminate later builds.

IA32 UEFI, Secure Boot, ARM/Apple Silicon, legacy CPUs without CPUID and every
physical PC model are outside this matrix. Failure to provide a usable RGB
framebuffer can still prevent MayKernel's desktop from starting. Linux rescue
requires a firmware console and supported input hardware. USB/NVMe/GPU/Wi-Fi
drivers and physical installation are later work, as requested.

Tests must distinguish software layout validation, QEMU boot verification and
physical-device tests. QEMU success does not establish arbitrary-PC support.
Existing ISO files and old recordings do not contain these new changes.

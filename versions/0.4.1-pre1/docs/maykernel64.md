# MayKernel64: native x86_64 port

## Boot path

The MayKernel payload is **ELF64 for AMD x86-64**, not an ELF32 binary with a
relabelled UI. GRUB's Multiboot1 protocol initially enters `boot64.S` in 32-bit
protected mode (both legacy BIOS and x64 UEFI). The bootstrap checks CPUID
long-mode support, creates its own four-level paging hierarchy, enables
CR4.PAE and EFER.LME, then enables paging and far-jumps to a 64-bit code
segment before invoking the complete MayKernel GUI/MayFS C code compiled with
`-m64 -mno-red-zone`.

The initial PML4/PDPT/PD mapping identity maps the first **64 GiB of physical
addresses** using 2 MiB pages. This is an address translation window, **not**
a claim that MayKernel can allocate or manage 64 GiB of memory. The early
physical page allocator still manages at most **128 MiB** of low memory. The
Multiboot RAM map is used to avoid reported reserved regions; high RAM is
included in total available-RAM reporting. File storage, PS/2 input, graphics
surface conversion and the same desktop UI are preserved.

MayKernel uses native 16-byte x86_64 IDT gates and an `iretq` timer IRQ
handler. The firmware framebuffer physical address is kept at 64-bit width
rather than truncated, and is usable if the mapped RGB framebuffer is entirely
below the initial 64 GiB identity-map ceiling. This early framebuffer mapping
is not a universal GPU driver and has not been verified on arbitrary hardware.

## Supported / unsupported

- Intended boot: BIOS on x86_64 CPUs; UEFI x64 with Secure Boot **off**.
- The Linux 7.3.1 x86_64 desktop and rescue entries remain independent of the
  custom kernel; on the dual ISO both images require 64-bit long mode.
- 32-bit-only x86 CPUs are no longer able to boot MayKernel: GRUB presents a
  clear compatibility message, not a misleading 32-bit recovery environment.
- IA32 UEFI, ARM, Secure Boot, arbitrary GPUs or all USB/NVMe/Wi-Fi hardware,
  and physical machine support are **not** claimed.

## Reproducible checks

```sh
make all
readelf -h build/mayos.elf                # ELF64 / Advanced Micro Devices X86-64
grub-file --is-x86-multiboot build/mayos.elf
make test                                 # real QEMU BIOS, GUI, input, MayFS
python3 scripts/uefi_smoke.py --custom-only --iso build/mayos.iso
make dual LINUX_VERSION=7.3.1
python3 scripts/boot_compat_test.py --dual32    # reject an unsupported 32-bit CPU
make test-dual LINUX_VERSION=7.3.1
make test-uefi LINUX_VERSION=7.3.1
```

CI archives the actual framebuffer screenshots, QEMU logs and test reports.
Results from an older 32-bit MayKernel ISO cannot be used to claim success.
No Release or website text should claim this architecture until tests for the
matching source revision and artifacts succeed.

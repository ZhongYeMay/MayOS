# Limine migration

MayOS 0.5.0-pre2-dev migrates the default ISO boot menu from GRUB to Limine.
The Limine ISO uses the current 64-bit Multiboot MayKernel entry unchanged,
then presents Linux x86_64 from the same menu when its kernel and initramfs are
staged. BIOS and x86_64 UEFI are both required before this path replaces the
published GRUB ISO.

Build a MayKernel-only prototype with a Limine release directory:

```sh
make all
python3 scripts/limine_iso.py --limine-dir /path/to/limine \
  --output build/mayos-limine.iso
```

For the dual-kernel image, pass the Linux payloads produced by `make dual` as
`--linux-kernel` and `--initramfs`. The branded image is deliberately a static
boot asset; no desktop code or network dependency runs in Limine.

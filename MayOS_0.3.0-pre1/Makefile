.DEFAULT_GOAL := all
CC := gcc
LD := ld
PYTHON ?= python3
GRUB_MKRESCUE ?= grub-mkrescue
GRUB_FILE ?= grub-file
QEMU ?= qemu-system-x86_64
QEMU64 ?= qemu-system-x86_64
LINUX_VERSION ?= latest
LINUX_JOBS ?= 4
BUSYBOX ?=
FASTFETCH ?=
BOOT_FIRMWARE ?= hybrid
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
CFLAGS := -m64 -DMAYOS_X86_64 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin -fno-strict-aliasing -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only -Ibuild -Ikernel
OBJECTS := build/boot64.o build/core.o build/fs.o build/gui.o build/galgame.o

# MayKernel art: linked as its own raw RGBA archive into the freestanding ELF.
# No SDL/WebP library, Linux ABI or webview code is linked into MayKernel.
# Set SUMMER_SITE=/absolute/path/to/galgame/site to include all 12 original assets.
ifneq ($(strip $(SUMMER_SITE)),)
CFLAGS += -DMAYGAME_WITH_ART -DMAYGAME_WITH_STORY
OBJECTS += build/summer_art.o
build/summer_story.h: scripts/pack_summer_story.js $(SUMMER_SITE)/story.js | build
	node scripts/pack_summer_story.js "$(SUMMER_SITE)/story.js" build/summer_story.h
build/font.h: build/summer_story.h
build/galgame.o: build/summer_story.h
build/summer_art.h: scripts/pack_summer_art.py $(wildcard $(SUMMER_SITE)/assets/retro/*.webp) | build
	$(PYTHON) scripts/pack_summer_art.py --site "$(SUMMER_SITE)" --output build
build/summer_art.o: build/summer_art.h
	$(LD) -m elf_x86_64 -r -b binary -o $@ build/summer_art.bin
build/galgame.o: build/summer_art.h
endif
.PHONY: all clean run test linux linux-update dual run-dual test-dual run-uefi test-uefi
all: build/mayos.iso build/mayos-disk.img
build:
	mkdir -p build
build/font.h: scripts/font.py $(wildcard kernel/*.c) | build
	$(PYTHON) scripts/font.py
build/cat_image.h: scripts/photo.py assets/easter-cat.jpg | build
	$(PYTHON) scripts/photo.py
build/gui.o: build/cat_image.h kernel/explorer_history.h kernel/galgame.h
build/core.o: kernel/framebuffer.h kernel/boot64.S
build/galgame.o: kernel/galgame.h
build/boot64.o: kernel/boot64.S | build
	$(CC) -m64 -c $< -o $@
build/%.o: kernel/%.c kernel/core.h kernel/io.h build/font.h
	$(CC) $(CFLAGS) -c $< -o $@
build/mayos.elf: $(OBJECTS) kernel/linker64.ld
	$(LD) -m elf_x86_64 -T kernel/linker64.ld -o $@ $(OBJECTS)
	$(GRUB_FILE) --is-x86-multiboot $@
build/mayos.iso: build/mayos.elf boot/grub/grub.cfg scripts/dual_iso.py
	$(PYTHON) scripts/dual_iso.py --custom-only --firmware "$(BOOT_FIRMWARE)" --grub-mkrescue '$(GRUB_MKRESCUE)'
build/mayos-disk.img: | build
	truncate -s 8M $@
run: all
	$(QEMU) -cpu max $(if $(QEMU_DATA_DIR),-L $(QEMU_DATA_DIR)) -m 128 -vga std -cdrom build/mayos.iso -drive file=build/mayos-disk.img,format=raw,if=ide,index=0 -boot d -serial file:build/serial.log -no-reboot
test: all
	$(PYTHON) scripts/smoke.py
	$(PYTHON) scripts/ui_smoke.py
	$(PYTHON) scripts/settings_smoke.py
	$(PYTHON) scripts/easter_smoke.py
# An explicit Linux build resolves latest stable at kernel.org. `dual` reuses
# verified local payloads; run `linux-update` when a newer stable is wanted.
linux: | build
	$(PYTHON) scripts/linux.py build --version "$(LINUX_VERSION)" --jobs "$(LINUX_JOBS)" $(if $(BUSYBOX),--busybox "$(BUSYBOX)") $(if $(FASTFETCH),--fastfetch "$(FASTFETCH)")
linux-update: | build
	$(PYTHON) scripts/linux.py build --version latest --jobs "$(LINUX_JOBS)" $(if $(BUSYBOX),--busybox "$(BUSYBOX)") $(if $(FASTFETCH),--fastfetch "$(FASTFETCH)")
dual: build/mayos.elf build/mayos-disk.img
	$(PYTHON) scripts/dual_iso.py --version "$(LINUX_VERSION)" --jobs "$(LINUX_JOBS)" --firmware "$(BOOT_FIRMWARE)" $(if $(BUSYBOX),--busybox "$(BUSYBOX)") $(if $(FASTFETCH),--fastfetch "$(FASTFETCH)") --grub-mkrescue '$(GRUB_MKRESCUE)'
run-dual: dual
	$(QEMU64) $(if $(QEMU_DATA_DIR),-L "$(QEMU_DATA_DIR)") -machine pc -accel tcg -cpu max -m 512 -vga std -cdrom build/mayos-dual.iso -drive file=build/mayos-disk.img,format=raw,if=ide,index=0 -boot d -serial stdio -no-reboot
test-dual: dual
	QEMU64="$(QEMU64)" $(PYTHON) scripts/dual_boot_smoke.py
run-uefi: dual
	test -f "$(OVMF_CODE)" && test -f "$(OVMF_VARS)"
	test -f build/mayos-uefi-vars.fd || cp "$(OVMF_VARS)" build/mayos-uefi-vars.fd
	$(QEMU64) $(if $(QEMU_DATA_DIR),-L "$(QEMU_DATA_DIR)") -machine pc -accel tcg -cpu max -m 512 -vga std -drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" -drive if=pflash,format=raw,file=build/mayos-uefi-vars.fd -cdrom build/mayos-dual.iso -drive file=build/mayos-disk.img,format=raw,if=ide,index=0 -boot d -serial stdio -no-reboot
test-uefi: dual
	OVMF_CODE="$(OVMF_CODE)" OVMF_VARS="$(OVMF_VARS)" QEMU64="$(QEMU64)" $(PYTHON) scripts/uefi_smoke.py
clean:
	rm -rf build/iso build/dual-iso build/*.o build/*.elf build/*.iso build/font.h build/cat_image.h build/summer_art.h build/summer_art.bin build/summer_story.h

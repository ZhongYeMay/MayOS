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
# Used by CI to boot two different versions of the installer prototype.
ifneq ($(strip $(INSTALLER_TEST_VERSION)),)
CFLAGS += -DMAYOS_VERSION=\"$(INSTALLER_TEST_VERSION)\"
endif
# Only used for the isolated CI fault-injection build; NEVER enable for releases.
EXCEPTION_SELFTEST ?= 0
ifeq ($(EXCEPTION_SELFTEST),1)
CFLAGS += -DMAYOS_EXCEPTION_SELFTEST
endif
# Only in an isolated, intentionally wedged QEMU ISO; never use for release.
FS_POWERLOSS_SELFTEST ?= 0
ifeq ($(FS_POWERLOSS_SELFTEST),1)
CFLAGS += -DMAYOS_FS_POWERLOSS_SELFTEST
endif
OBJECTS := build/boot64.o build/core.o build/acpi.o build/pci.o build/mmio.o build/ahci.o build/nvme.o build/xhci.o build/e1000.o build/hda.o build/fs.o build/gui.o build/galgame.o build/update.o build/net.o build/net_ipv4.o build/net_dhcp.o build/sha256.o build/net_tcp.o build/net_tcp_retry.o build/security_clock.o build/security_entropy.o build/security_tls.o build/security_tls_provider.o

# This is an OPTIONAL vendor-link TEST, not an HTTPS enable switch.
# BEARSSL_SOURCE must be a locally checked-out, pinned upstream source tree.
# Default MayKernel build uses no third-party TLS provider and blocks HTTPS.
BEARSSL_SOURCE ?=
BEARSSL_LIB :=
ifneq ($(strip $(BEARSSL_SOURCE)),)
CFLAGS += -DMAYOS_USE_BEARSSL -I$(BEARSSL_SOURCE)/inc
OBJECTS += build/security_tls_compat.o
BEARSSL_LIB := build/libbearssl.a
build/libbearssl.a: scripts/build_bearssl_freestanding.sh $(BEARSSL_SOURCE)/inc/bearssl.h | build
	bash scripts/build_bearssl_freestanding.sh "$(BEARSSL_SOURCE)" $@
endif

# Isolated native BearSSL X.509 validation using a CI-only CA/certificate.
# This MUST NOT be enabled in production: the CA and wall-clock are test data.
MAYOS_TLS_X509_DIAGNOSTIC ?= 0
ifeq ($(MAYOS_TLS_X509_DIAGNOSTIC),1)
ifeq ($(strip $(BEARSSL_SOURCE)),)
$(error MAYOS_TLS_X509_DIAGNOSTIC requires the pinned BEARSSL_SOURCE checkout)
endif
CFLAGS += -DMAYOS_TLS_X509_DIAGNOSTIC
OBJECTS += build/security_tls_x509_fixture.o
build/security_tls_x509_fixture.o: build/tls_x509_fixture.h
endif

# Optional Rust/C hybrid kernel build. Default C mode keeps all existing
# release and QEMU workflows unchanged until the Rust path is fully accepted.
# Requires rustup target add x86_64-unknown-none (no userspace, no allocator).
MAYOS_RUST ?= 0
MAYOS_RELEASE_SIGNATURE ?= 0
# Combine the signed byte gate with physical ATA staging only on marker-guarded
# 8MiB QEMU disks. No network connection or installation/bootloader writes.
MAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC ?= 0
ifeq ($(MAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC),1)
override MAYOS_SIGNED_STREAM_DIAGNOSTIC := 1
override MAYOS_STAGE_DISK_DIAGNOSTIC := 1
CFLAGS += -DMAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC
endif
# Read-only test gate. Never enable in publish/release workflows.
MAYOS_SIGNED_STREAM_DIAGNOSTIC ?= 0
ifeq ($(MAYOS_SIGNED_STREAM_DIAGNOSTIC),1)
override MAYOS_RELEASE_SIGNATURE := 1
CFLAGS += -DMAYOS_SIGNED_STREAM_DIAGNOSTIC
endif
RUSTC ?= rustc
CARGO ?= cargo
RUST_TARGET ?= x86_64-unknown-none
# The strict Ed25519 verifier is an OPTIONAL Rust module. Never interpret
# cryptographic verification under an arbitrary key as release trust.
MAYOS_RELEASE_PUBKEY_HEX ?=
MAYOS_ALLOW_TEST_RELEASE_KEY ?= 0
ifeq ($(MAYOS_RELEASE_SIGNATURE),1)
override MAYOS_RUST := 1
CFLAGS += -DMAYOS_RELEASE_SIGNATURE
build/release_pubkey.h: scripts/pin_release_public_key.py FORCE | build
	MAYOS_ALLOW_TEST_RELEASE_KEY=$(MAYOS_ALLOW_TEST_RELEASE_KEY) $(PYTHON) scripts/pin_release_public_key.py --public-key "$(MAYOS_RELEASE_PUBKEY_HEX)" --output $@
build/update.o: build/release_pubkey.h
endif
# An intentionally non-release, opt-in ATA write diagnostic on an 8MiB
# MayFS test disk with an explicit marker. No real A/B partition installer.
MAYOS_AB_DISK_DIAGNOSTIC ?= 0
RUST_AB_CFG :=
RUST_AB_FEATURE :=
ifeq ($(MAYOS_AB_DISK_DIAGNOSTIC),1)
override MAYOS_RUST := 1
CFLAGS += -DMAYOS_AB_DISK_DIAGNOSTIC
OBJECTS += build/ab_journal_disk.o
RUST_AB_CFG := --cfg 'feature="ab-disk-diagnostic"'
RUST_AB_FEATURE :=,ab-disk-diagnostic
endif
# Non-release streaming-store diagnostic, confined to a marked 8MiB QEMU
# disk. User builds contain neither raw staging writer nor shell controls.
MAYOS_STAGE_DISK_DIAGNOSTIC ?= 0
ifeq ($(MAYOS_STAGE_DISK_DIAGNOSTIC),1)
override MAYOS_RUST := 1
CFLAGS += -DMAYOS_STAGE_DISK_DIAGNOSTIC
OBJECTS += build/update_stage_codec.o build/update_stage_disk.o
endif
RUST_LIB :=
RUST_TASK_SOURCES := rust/task_manager/view.rs rust/maykernel_policy/src/procfs.rs
ifeq ($(MAYOS_RUST),1)
CFLAGS += -DMAYOS_RUST_CORE
OBJECTS += build/update_stream_integrity.o
RUST_LIB := build/libmaykernel_rust.a
ifeq ($(MAYOS_RELEASE_SIGNATURE),1)
build/libmaykernel_rust.a: rust/maykernel_policy/src/lib.rs rust/maykernel_policy/src/release_signature.rs rust/maykernel_policy/src/ab_policy.rs rust/maykernel_policy/src/ab_policy/disk.rs rust/maykernel_policy/src/release_url.rs rust/maykernel_policy/src/http_framing.rs rust/maykernel_policy/src/http_body_budget.rs rust/maykernel_policy/Cargo.toml $(RUST_TASK_SOURCES) build/config-flags.txt | build
	CARGO_TARGET_DIR=$(CURDIR)/build/cargo RUSTFLAGS='-C panic=abort -C no-redzone=yes -C relocation-model=static --cfg curve25519_dalek_backend="serial"' $(CARGO) build --locked --manifest-path rust/maykernel_policy/Cargo.toml --target $(RUST_TARGET) --release --no-default-features --features release-signature$(RUST_AB_FEATURE)
	cp build/cargo/$(RUST_TARGET)/release/libmaykernel_policy.a $@
else
build/libmaykernel_rust.a: rust/maykernel_policy/src/lib.rs rust/maykernel_policy/src/ab_policy.rs rust/maykernel_policy/src/ab_policy/disk.rs rust/maykernel_policy/src/release_url.rs rust/maykernel_policy/src/http_framing.rs rust/maykernel_policy/src/http_body_budget.rs $(RUST_TASK_SOURCES) build/config-flags.txt | build
	$(RUSTC) --edition=2021 --crate-name maykernel_policy \
	  --crate-type staticlib --target $(RUST_TARGET) -C panic=abort \
	  -C opt-level=s -C codegen-units=1 -C relocation-model=static \
	  -C no-redzone=yes $(RUST_AB_CFG) $< -o $@
endif
endif

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
.PHONY: test-signed-stream-ata-qemu test-signed-stream-qemu test-stream-integrity-qemu test-stream-integrity test-http-budget-qemu test-http-guard-qemu test-tls-x509-qemu test-rust-url-qemu test-bearssl-qemu test-rust-host test-rust-qemu test-rust-bearssl-qemu test-rust-video test-ab-journal-qemu test-stage-codec test-stage-qemu
.PHONY: all clean run test test-update test-net test-net-codec test-dhcp test-tcp test-tcp-retry test-sha256 test-update-verify test-security test-tls-qemu linux linux-update dual run-dual test-dual run-uefi test-uefi
all: build/mayos.iso build/mayos-disk.img
# Build flags and pinned key are part of the artifact identity. Never reuse
# signed-mode objects or stale root keys after switching configurations.
.PHONY: FORCE
FORCE:
build:
	mkdir -p build
build/config-flags.txt: FORCE | build
	@printf '%s\n' '$(CFLAGS)' 'RUST=$(MAYOS_RUST)' 'SIGNATURE=$(MAYOS_RELEASE_SIGNATURE)' 'KEY=$(MAYOS_RELEASE_PUBKEY_HEX)' 'TESTKEY=$(MAYOS_ALLOW_TEST_RELEASE_KEY)' 'ABDIAG=$(MAYOS_AB_DISK_DIAGNOSTIC)' 'STAGEDIAG=$(MAYOS_STAGE_DISK_DIAGNOSTIC)' 'SIGNSTREAMDIAG=$(MAYOS_SIGNED_STREAM_DIAGNOSTIC)' 'SIGNEDATADIAG=$(MAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC)' 'X509DIAG=$(MAYOS_TLS_X509_DIAGNOSTIC)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi
build/font.h: scripts/font.py $(wildcard kernel/*.c) kernel/ime.h kernel/locale.h kernel/locale_strings.h linux/installer-ui.c | build
	$(PYTHON) scripts/font.py
build/ime_data.h: scripts/ime_assets.py assets/ime/mandarin.tsv assets/ime/phrases.tsv | build
	$(PYTHON) scripts/ime_assets.py
build/gui.o: build/ime_data.h kernel/ime.h kernel/utf8.h
build/fs.o: kernel/utf8.h
build/boot_logo.h: scripts/boot_art.py assets/limine-mayos-brand.png | build
	$(PYTHON) scripts/boot_art.py $@
build/cat_image.h: scripts/photo.py assets/easter-cat.jpg | build
	$(PYTHON) scripts/photo.py
# Actual licensed Material Symbols Rounded glyphs are rasterized at build time.
# No external font or browser runtime is required in the finished ISO.
build/material_icons.h: assets/material_symbols.py | build
	$(PYTHON) assets/material_symbols.py
build/gui.o: build/cat_image.h build/material_icons.h build/boot_logo.h kernel/explorer_history.h kernel/galgame.h kernel/update.h kernel/net.h kernel/security_tls.h kernel/rust_bridge.h
build/ab_journal_disk.o: kernel/rust_bridge.h
build/update_stage_codec.o: kernel/update_stage_codec.h
build/update_stage_disk.o: kernel/update_stage_codec.h kernel/sha256.h
build/update.o: kernel/update.h kernel/version.h kernel/sha256.h kernel/rust_bridge.h
build/sha256.o: kernel/sha256.h
build/update_stream_integrity.o: kernel/update_stream_integrity.h kernel/sha256.h kernel/rust_bridge.h
build/core.o: kernel/framebuffer.h kernel/boot64.S kernel/net.h kernel/security_tls.h
build/net.o: kernel/net.h kernel/net_ipv4.h kernel/net_dhcp.h kernel/net_tcp.h kernel/net_tcp_retry.h kernel/io.h
build/net_dhcp.o: kernel/net_dhcp.h
build/net_tcp.o: kernel/net_tcp.h kernel/net_ipv4.h
build/net_tcp_retry.o: kernel/net_tcp_retry.h
build/net_ipv4.o: kernel/net_ipv4.h
build/security_tls.o: kernel/security_tls.h kernel/security_tls_provider.h kernel/security_clock.h kernel/security_entropy.h
build/security_tls_provider.o: kernel/security_tls_provider.h kernel/security_tls.h
build/security_clock.o: kernel/security_clock.h kernel/io.h
build/security_entropy.o: kernel/security_entropy.h
build/core.o: kernel/framebuffer.h kernel/boot64.S kernel/task_manager.h
build/core.o build/gui.o build/fs.o: kernel/procfs.h
build/gui.o: kernel/task_manager.h
build/acpi.o: kernel/acpi.h
build/pci.o: kernel/pci.h kernel/io.h
build/ahci.o: kernel/ahci.h kernel/pci.h
build/nvme.o: kernel/nvme.h kernel/pci.h kernel/mmio.h
build/xhci.o: kernel/xhci.h kernel/pci.h kernel/mmio.h kernel/task_manager.h
build/mmio.o: kernel/mmio.h
build/gui.o: kernel/xhci.h kernel/nvme.h
build/core.o: kernel/xhci.h kernel/acpi.h
build/e1000.o: kernel/e1000.h kernel/pci.h
build/net.o: kernel/pci.h kernel/e1000.h
build/fs.o: kernel/pci.h kernel/ahci.h kernel/nvme.h
build/hda.o: kernel/hda.h kernel/pci.h
build/galgame.o: kernel/galgame.h
build/boot64.o: kernel/boot64.S build/config-flags.txt | build
	$(CC) -m64 $(if $(filter 1,$(MAYOS_RUST)),-DMAYOS_RUST_CORE) -c $< -o $@
build/%.o: kernel/%.c kernel/core.h kernel/io.h kernel/version.h build/font.h build/config-flags.txt
	$(CC) $(CFLAGS) -c $< -o $@
build/mayos.elf: $(OBJECTS) $(RUST_LIB) $(BEARSSL_LIB) kernel/linker64.ld
	$(LD) -m elf_x86_64 -T kernel/linker64.ld -o $@ $(OBJECTS) $(RUST_LIB) $(BEARSSL_LIB)
	$(GRUB_FILE) --is-x86-multiboot $@
build/mayos.iso: build/mayos.elf boot/grub/grub.cfg boot/grub/menu.cfg boot/grub/themes/mayos/theme.txt scripts/dual_iso.py scripts/boot_art.py docs/third-party/Unicode-IME.txt docs/third-party/Noto-CJK-copyright.txt
	$(PYTHON) scripts/dual_iso.py --custom-only --firmware "$(BOOT_FIRMWARE)" --grub-mkrescue '$(GRUB_MKRESCUE)'
build/mayos-disk.img: | build
	truncate -s 8M $@
run: all
	$(QEMU) -cpu max $(if $(QEMU_DATA_DIR),-L $(QEMU_DATA_DIR)) -m 128 -vga std -cdrom build/mayos.iso -drive file=build/mayos-disk.img,format=raw,if=ide,index=0 -boot d -serial file:build/serial.log -no-reboot
build/update_manifest_test: tests/update_manifest_test.c kernel/update.c kernel/update.h kernel/sha256.c kernel/version.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/update_manifest_test.c kernel/update.c kernel/sha256.c -o $@
test-update: build/update_manifest_test
	./build/update_manifest_test
build/sha256_test: tests/sha256_test.c kernel/sha256.c kernel/sha256.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/sha256_test.c kernel/sha256.c -o $@
test-sha256: build/sha256_test
	./build/sha256_test
build/update_stream_integrity_test: tests/update_stream_integrity_test.c kernel/update_stream_integrity.c kernel/update_stream_integrity.h kernel/rust_bridge.h kernel/sha256.c kernel/sha256.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -DMAYOS_RUST_CORE -Ikernel tests/update_stream_integrity_test.c kernel/update_stream_integrity.c kernel/sha256.c -o $@
test-stream-integrity: build/update_stream_integrity_test
	./build/update_stream_integrity_test
build/update_verify_test: tests/update_verify_test.c kernel/update.c kernel/update.h kernel/sha256.c kernel/sha256.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/update_verify_test.c kernel/update.c kernel/sha256.c -o $@
test-update-verify: build/update_verify_test
	./build/update_verify_test
build/security_tls_test: tests/security_tls_test.c kernel/security_tls.c kernel/security_tls_provider.c kernel/security_clock.c kernel/security_entropy.c kernel/core.h kernel/io.h kernel/security_tls.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/security_tls_test.c kernel/security_tls.c kernel/security_tls_provider.c kernel/security_clock.c kernel/security_entropy.c -o $@
test-security: build/security_tls_test
	./build/security_tls_test
build/net_ipv4_test: tests/net_ipv4_test.c kernel/net_ipv4.c kernel/net_ipv4.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/net_ipv4_test.c kernel/net_ipv4.c -o $@
test-net-codec: build/net_ipv4_test
	./build/net_ipv4_test
build/net_tcp_test: tests/net_tcp_test.c kernel/net_tcp.c kernel/net_ipv4.c kernel/net_tcp.h kernel/net_ipv4.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/net_tcp_test.c kernel/net_tcp.c kernel/net_ipv4.c -o $@
test-tcp: build/net_tcp_test
	./build/net_tcp_test
build/net_tcp_retry_test: tests/net_tcp_retry_test.c kernel/net_tcp_retry.c kernel/net_tcp_retry.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/net_tcp_retry_test.c kernel/net_tcp_retry.c -o $@
test-tcp-retry: build/net_tcp_retry_test
	./build/net_tcp_retry_test
build/net_dhcp_test: tests/net_dhcp_test.c kernel/net_dhcp.c kernel/net_dhcp.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/net_dhcp_test.c kernel/net_dhcp.c -o $@
test-dhcp: build/net_dhcp_test
	./build/net_dhcp_test
test-tls-qemu: all test-security
	$(PYTHON) scripts/tls_smoke.py
# Rust host unit tests run with std only in the test harness. The target
# MayKernel library remains no_std with no allocator.
build/rust_manifest_tests: rust/maykernel_policy/src/lib.rs rust/maykernel_policy/src/ab_policy.rs rust/maykernel_policy/src/ab_policy/disk.rs rust/maykernel_policy/src/release_url.rs rust/maykernel_policy/src/http_framing.rs rust/maykernel_policy/src/http_body_budget.rs | build
	$(RUSTC) --edition=2021 --crate-name maykernel_policy --test $< -o $@
test-rust-host: build/rust_manifest_tests
	./build/rust_manifest_tests
# The tests use the published RFC 8032 signature vector, not a release key.
.PHONY: test-ed25519-host
test-ed25519-host:
	$(CARGO) test --locked --manifest-path rust/maykernel_policy/Cargo.toml --no-default-features --features release-signature
.PHONY: test-ed25519-qemu
test-ed25519-qemu: all test-ed25519-host
	@test "$(MAYOS_RELEASE_SIGNATURE)" = "1" || (echo "Set MAYOS_RELEASE_SIGNATURE=1" >&2; exit 1)
	$(PYTHON) scripts/ed25519_smoke.py

build/update_stage_codec_test: tests/update_stage_codec_test.c kernel/update_stage_codec.c kernel/update_stage_codec.h kernel/core.h | build
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -Ikernel tests/update_stage_codec_test.c kernel/update_stage_codec.c -o $@
test-stage-codec: build/update_stage_codec_test
	./build/update_stage_codec_test
test-stage-qemu: all test-stage-codec
	@test "$(MAYOS_STAGE_DISK_DIAGNOSTIC)" = "1" || (echo "Set MAYOS_STAGE_DISK_DIAGNOSTIC=1 for isolated disk fixture" >&2; exit 1)
	$(PYTHON) scripts/update_stage_smoke.py
test-ab-journal-qemu: all test-rust-host
	@test "$(MAYOS_AB_DISK_DIAGNOSTIC)" = "1" || (echo "Set MAYOS_AB_DISK_DIAGNOSTIC=1" >&2; exit 1)
	$(PYTHON) scripts/ab_journal_smoke.py
test-signed-stream-ata-qemu: all test-stream-integrity test-stage-codec test-rust-host
	@test "$(MAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC)" = "1" || (echo "Set MAYOS_SIGNED_ATA_STREAM_DIAGNOSTIC=1 on isolated QEMU only" >&2; exit 1)
	@test -n "$(MAYOS_RELEASE_PUBKEY_HEX)" || (echo "Pinned public key is required" >&2; exit 1)
	$(PYTHON) scripts/signed_stream_ata_smoke.py --package-dir "$(PACKAGE_DIR)" --wrong-hash-package-dir "$(WRONG_HASH_PACKAGE_DIR)"
test-signed-stream-qemu: all test-stream-integrity test-rust-host
	@test "$(MAYOS_SIGNED_STREAM_DIAGNOSTIC)" = "1" || (echo "Set MAYOS_SIGNED_STREAM_DIAGNOSTIC=1 for QEMU only" >&2; exit 1)
	@test -n "$(MAYOS_RELEASE_PUBKEY_HEX)" || (echo "Pinned public key is required" >&2; exit 1)
	$(PYTHON) scripts/signed_stream_smoke.py --package-dir "$(PACKAGE_DIR)"
test-stream-integrity-qemu: all test-stream-integrity test-rust-host
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for Rust+C QEMU digest test" >&2; exit 1)
	$(PYTHON) scripts/stream_integrity_smoke.py
test-http-budget-qemu: all test-rust-host
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for body budget QEMU" >&2; exit 1)
	$(PYTHON) scripts/http_body_budget_smoke.py
test-http-guard-qemu: all test-rust-host
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for Rust HTTP preflight QEMU test" >&2; exit 1)
	$(PYTHON) scripts/http_guard_smoke.py
test-rust-url-qemu: all test-rust-host
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for native Rust Release URL test" >&2; exit 1)
	$(PYTHON) scripts/release_url_smoke.py
test-rust-qemu: all test-rust-host
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for native Rust integration" >&2; exit 1)
	$(PYTHON) scripts/rust_smoke.py
test-rust-video: all
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for native Rust video" >&2; exit 1)
	$(PYTHON) scripts/rust_video.py
test-rust-bearssl-qemu: all test-rust-host
	@test "$(MAYOS_RUST)" = "1" || (echo "Set MAYOS_RUST=1 for Rust/BearSSL hybrid test" >&2; exit 1)
	@test -n "$(BEARSSL_SOURCE)" || (echo "Set BEARSSL_SOURCE to pinned upstream checkout" >&2; exit 1)
	$(PYTHON) scripts/rust_bearssl_smoke.py

test-tls-x509-qemu: all
	@test "$(MAYOS_TLS_X509_DIAGNOSTIC)" = "1" || (echo "Set MAYOS_TLS_X509_DIAGNOSTIC=1 for test-only X509" >&2; exit 1)
	$(PYTHON) scripts/tls_x509_fixture_smoke.py
test-bearssl-qemu: all test-security
	@test -n "$(BEARSSL_SOURCE)" || (echo "Provide pinned BEARSSL_SOURCE checkout" >&2; exit 1)
	$(PYTHON) scripts/tls_provider_smoke.py
test-net: all test-net-codec test-dhcp test-tcp test-tcp-retry test-security
	$(PYTHON) scripts/net_smoke.py
	$(PYTHON) scripts/tls_smoke.py
test: all test-update test-net-codec test-dhcp test-tcp test-tcp-retry test-sha256 test-update-verify test-security
	$(PYTHON) scripts/smoke.py
	$(PYTHON) scripts/ahci_smoke.py
	$(PYTHON) scripts/nvme_smoke.py
	$(PYTHON) scripts/pci_smoke.py
	$(PYTHON) scripts/ui_smoke.py
	$(PYTHON) scripts/settings_smoke.py
	$(PYTHON) scripts/net_smoke.py
	$(PYTHON) scripts/net_smoke.py --model e1000
	$(PYTHON) scripts/net_smoke.py --model e1000e
	$(PYTHON) scripts/tls_smoke.py
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
	rm -rf build/iso build/dual-iso build/*.o build/*.elf build/*.iso build/*.a build/rust_manifest_tests build/update_stage_codec_test build/config-flags.txt build/release_pubkey.h build/tls_x509_fixture.h build/font.h build/ime_data.h build/cat_image.h build/boot_logo.h build/material_icons.h build/summer_art.h build/summer_art.bin build/summer_story.h

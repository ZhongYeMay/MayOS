#!/usr/bin/env bash
# Optional helper for packages unpacked without administrator privileges.
# Set MAYOS_TOOL_ROOT to the directory containing their usr/ tree before sourcing.
if [ -n "${MAYOS_TOOL_ROOT:-}" ] && [ -d "$MAYOS_TOOL_ROOT" ]; then
  export PATH="$MAYOS_TOOL_ROOT/usr/bin:$MAYOS_TOOL_ROOT/bin:$PATH"
  export LD_LIBRARY_PATH="$MAYOS_TOOL_ROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export LIBRARY_PATH="$MAYOS_TOOL_ROOT/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
  export CPATH="$MAYOS_TOOL_ROOT/usr/include${CPATH:+:$CPATH}"
  export QEMU_MODULE_DIR="$MAYOS_TOOL_ROOT/usr/lib/x86_64-linux-gnu/qemu"
  export QEMU_DATA_DIR="$MAYOS_TOOL_ROOT/usr/share/qemu"
  export GRUB_MKRESCUE="grub-mkrescue -d \"$MAYOS_TOOL_ROOT/usr/lib/grub/i386-pc\" --fonts= --themes= --locales="
  export GRUB_PC_DIR="$MAYOS_TOOL_ROOT/usr/lib/grub/i386-pc"
  if [ -d "$MAYOS_TOOL_ROOT/usr/lib/grub/x86_64-efi" ]; then
    export GRUB_EFI_DIR="$MAYOS_TOOL_ROOT/usr/lib/grub/x86_64-efi"
  fi
  if [ -f "$MAYOS_TOOL_ROOT/usr/share/OVMF/OVMF_CODE_4M.fd" ]; then
    export OVMF_CODE="$MAYOS_TOOL_ROOT/usr/share/OVMF/OVMF_CODE_4M.fd"
    export OVMF_VARS="$MAYOS_TOOL_ROOT/usr/share/OVMF/OVMF_VARS_4M.fd"
  fi
  if [ -d "$MAYOS_TOOL_ROOT/usr/share/bison" ]; then
    export BISON_PKGDATADIR="$MAYOS_TOOL_ROOT/usr/share/bison"
  fi
  if [ -f "$MAYOS_TOOL_ROOT/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc" ]; then
    export MAYOS_FONT="$MAYOS_TOOL_ROOT/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
  fi
fi

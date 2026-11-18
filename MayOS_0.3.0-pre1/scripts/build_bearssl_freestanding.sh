#!/usr/bin/env bash
# Build a pinned official BearSSL checkout as freestanding ELF64 objects.
# MayKernel use is EXPERIMENTAL AND FAIL-CLOSED: NOT HTTPS capability.
set -euo pipefail
if [[ $# -ne 2 ]]; then
  echo "usage: $0 <official-BearSSL-git-tree> <archive>" >&2
  exit 2
fi
source_dir="$(cd "$1" && pwd -P)"
out="$2"
pin="79c060eea3eea1257797f15ea1608a9a9923aa6f"
actual="$(git -C "$source_dir" rev-parse HEAD)"
if [[ "$actual" != "$pin" ]]; then
  echo "Refusing unexpected BearSSL revision: $actual (expected $pin)" >&2
  exit 1
fi
if [[ ! -f "$source_dir/inc/bearssl.h" || ! -d "$source_dir/src" ]]; then
  echo "Not a valid BearSSL source tree" >&2
  exit 1
fi
mkdir -p "$(dirname "$out")"
out="$(cd "$(dirname "$out")" && pwd -P)/$(basename "$out")"
work="$(dirname "$out")/bearssl-objects"
rm -rf "$work"
mkdir -p "$work"
files=0
while IFS= read -r -d '' file; do
  relative="$(printf '%s' "$file" | sed "s|^$source_dir/src/||")"
  # Never link Unix /dev/urandom, getentropy or syscalls into MayKernel.
  # A separate native seeder explicitly returns NULL (entropy unavailable).
  if [[ "$relative" == "rand/sysrng.c" ]]; then
    continue
  fi
  object="$work/$(printf '%s' "$relative" | sed 's/\.c$/.o/')"
  mkdir -p "$(dirname "$object")"
  gcc -std=c99 -O2 -m64 -ffreestanding -fno-builtin \
    -fno-stack-protector -fno-pie -fno-pic \
    -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only \
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 \
    -DBR_USE_WIN32_RAND=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 \
    -Dmemcpy=maybear_memcpy -Dmemset=maybear_memset \
    -Dmemmove=maybear_memmove -Dmemcmp=maybear_memcmp \
    -Dstrlen=maybear_strlen \
    -I"$source_dir/inc" -I"$source_dir/src" \
    -c "$file" -o "$object"
  files=$((files+1))
done < <(find "$source_dir/src" -name '*.c' -type f -print0 | sort -z)
if (( files < 100 )); then
  echo "BearSSL source tree unexpectedly small ($files C files)" >&2
  exit 1
fi
rm -f "$out"
find "$work" -name '*.o' -print0 | sort -z | xargs -0 ar rcs "$out"
echo "PASS: $files pinned BearSSL C files compiled to freestanding ELF64"
nm "$out" | grep -F 'br_ssl_client_init_full' >/dev/null
echo "PASS: genuine BearSSL client engine symbol available"

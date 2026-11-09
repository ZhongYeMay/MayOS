#!/bin/sh
# MayOS Linux game-package installer. Does NOT launch incomplete ports.
set -eu
REPO="ZhongYeMay/MayOS"
GAME_ID="summer-letters"
DEST="${XDG_DATA_HOME:-$HOME/.local/share}/mayos/games/$GAME_ID"
RELEASE="${MAYOS_GAME_RELEASE:-}"
if [ -z "$RELEASE" ]; then
  echo "No verified native game release configured. Installation is disabled." >&2
  exit 2
fi
case "$RELEASE" in *[!A-Za-z0-9._-]*|'') echo "Invalid release tag" >&2; exit 2;; esac
for cmd in curl sha256sum tar mktemp; do command -v "$cmd" >/dev/null 2>&1 || { echo "Missing dependency: $cmd" >&2; exit 2; }; done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
base="https://github.com/$REPO/releases/download/$RELEASE"
curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
  "$base/summer-letters-linux-x86_64.tar.gz" -o "$tmp/game.tar.gz"
curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
  "$base/summer-letters-linux-x86_64.sha256" -o "$tmp/game.sha256"
expected="$(awk 'NR==1 {print $1}' "$tmp/game.sha256")"
case "$expected" in *[!a-fA-F0-9]*|'') echo "Invalid checksum" >&2; exit 3;; esac
[ "${#expected}" -eq 64 ] || { echo "Invalid checksum length" >&2; exit 3; }
actual="$(sha256sum "$tmp/game.tar.gz" | awk '{print $1}')"
[ "$expected" = "$actual" ] || { echo "Checksum mismatch" >&2; exit 3; }
mkdir -p "$DEST"
tar -tzf "$tmp/game.tar.gz" | while IFS= read -r name; do
  case "$name" in /*|../*|*/../*|*/..|..) echo "Unsafe archive path" >&2; exit 4;; esac
done
# Extract into staging and activate only if all expected files exist.
mkdir -p "$tmp/stage"
tar -xzf "$tmp/game.tar.gz" -C "$tmp/stage" --no-same-owner --no-same-permissions
[ -f "$tmp/stage/manifest.json" ] || { echo "Missing game manifest" >&2; exit 4; }
[ -f "$tmp/stage/bin/summer-letters" ] || { echo "Missing native executable" >&2; exit 4; }
[ -f "$tmp/stage/VERIFIED_COMPLETE" ] || { echo "Game not certified complete" >&2; exit 4; }
mkdir -p "$DEST/versions"
cp -R "$tmp/stage/." "$DEST/versions/$RELEASE"
echo "Installed verified native game at $DEST/versions/$RELEASE"

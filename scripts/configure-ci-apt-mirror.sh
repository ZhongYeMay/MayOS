#!/usr/bin/env bash
# CI only. Select reachable Ubuntu archive with signed metadata and package indices.
set -euo pipefail
. /etc/os-release
codename="$VERSION_CODENAME"
if [[ "$ID" != ubuntu || -z "$codename" ]]; then
  echo "Requires Ubuntu with VERSION_CODENAME" >&2
  exit 2
fi
if [[ -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
  sudo mv /etc/apt/sources.list.d/ubuntu.sources /tmp/mayos-original-ubuntu.sources
fi
if [[ -f /etc/apt/sources.list ]]; then
  sudo mv /etc/apt/sources.list /tmp/mayos-original-sources.list
fi
selected=''
for uri in \
  "https://archive.ubuntu.com/ubuntu" \
  "https://mirrors.mit.edu/ubuntu" \
  "https://mirror.umd.edu/ubuntu" \
  "https://mirror.math.princeton.edu/pub/ubuntu"; do
  echo "Checking Ubuntu mirror $uri"
  if ! curl --fail --location --silent --show-error \
      --connect-timeout 4 --max-time 10 --retry 0 \
      "$uri/dists/$codename/InRelease" \
      --output /tmp/mayos-ubuntu-InRelease; then
    continue
  fi
  if ! grep -q '^-----BEGIN PGP SIGNED MESSAGE-----' \
      /tmp/mayos-ubuntu-InRelease; then
    echo "Refusing incomplete Release metadata for $uri" >&2
    continue
  fi
  sudo tee /etc/apt/sources.list.d/mayos-ci-ubuntu.sources >/dev/null <<EOF
Types: deb
URIs: $uri/
Suites: $codename $codename-updates $codename-backports $codename-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
  sudo rm -rf /var/lib/apt/lists/*
  sudo apt-get clean
  # apt-get update may return success while producing failed-fetch warnings.
  if sudo apt-get update -o Acquire::Retries=1 -o Acquire::ForceIPv4=true \
        2>&1 | tee /tmp/mayos-apt-update.log; then
    if ! grep -Eq '^(W: Failed to fetch|E:)' /tmp/mayos-apt-update.log \
       && apt-cache show qemu-system-x86 >/dev/null 2>&1 \
       && apt-cache show grub-pc-bin >/dev/null 2>&1; then
      selected="$uri"
      break
    fi
  fi
  echo "Mirror $uri did not provide usable package indices" >&2
done
if [[ -z "$selected" ]]; then
  echo "No reachable, signed Ubuntu package repository with required packages" >&2
  exit 1
fi
echo "Using signed Ubuntu archive: $selected"
if apt-cache policy | grep -Fq 'mirror+file:'; then
  echo "Old hosted-runner mirror+file source remains active" >&2
  exit 1
fi
echo "PASS: apt uses current signed Ubuntu metadata over HTTPS"

#!/usr/bin/env bash
# Replace hosted-runner mirror+file apt indirection; always retain Ubuntu package signatures.
set -euo pipefail
. /etc/os-release
codename="$VERSION_CODENAME"
if [[ "$ID" != ubuntu || -z "$codename" ]]; then
  echo "Requires Ubuntu with VERSION_CODENAME" >&2
  exit 2
fi

# Probe signed Release metadata rather than a mirror home page.
selected=''
for uri in \
  "https://mirror.math.princeton.edu/pub/ubuntu" \
  "https://mirrors.mit.edu/ubuntu" \
  "https://mirrors.kernel.org/ubuntu" \
  "https://mirror.umd.edu/ubuntu"; do
  echo "Checking Ubuntu mirror $uri"
  if curl --fail --location --silent --show-error \
      --connect-timeout 5 --max-time 15 --retry 1 \
      "$uri/dists/$codename/InRelease" \
      --output /tmp/mayos-ubuntu-InRelease &&
      grep -q '^-----BEGIN PGP SIGNED MESSAGE-----' /tmp/mayos-ubuntu-InRelease; then
    selected="$uri"
    break
  fi
done
if [[ -z "$selected" ]]; then
  echo "No reachable signed Ubuntu mirror found; refusing stale cache." >&2
  exit 1
fi
echo "Using signed Ubuntu mirror: $selected"

# Updating /etc/apt/sources.list alone does not change hosted Ubuntu 24.04
# mirror+file:/etc/apt/apt-mirrors.txt (which points to azure.archive.ubuntu.com).
if [[ -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
  sudo mv /etc/apt/sources.list.d/ubuntu.sources /tmp/mayos-original-ubuntu.sources
fi
if [[ -f /etc/apt/sources.list ]]; then
  sudo mv /etc/apt/sources.list /tmp/mayos-original-sources.list
fi
sudo tee /etc/apt/sources.list.d/mayos-ci-ubuntu.sources >/dev/null <<EOF
Types: deb
URIs: $selected/
Suites: $codename $codename-updates $codename-backports $codename-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

# Eliminate obsolete mirror+file package indices and fetch fresh signed metadata.
sudo rm -rf /var/lib/apt/lists/*
sudo apt-get clean
sudo apt-get update -o Acquire::Retries=3 -o Acquire::ForceIPv4=true
if apt-cache policy | grep -Fq 'mirror+file:'; then
  echo "Old mirror+file apt source remains active" >&2
  exit 1
fi
echo "PASS: apt uses current signed Ubuntu metadata over HTTPS"

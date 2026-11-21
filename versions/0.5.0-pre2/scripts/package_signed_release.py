#!/usr/bin/env python3
"""Offline Ed25519-signed MayOS Release metadata producer.

Accepts an EXISTING image (including an ISO) and outputs exactly:
 Update.may  – canonical ASCII metadata compatible with MayKernel's parser
 Update.sig  – 64-byte detached Ed25519 signature over raw Update.may

No signing secret is written to the output or repository. No unsigned
public key is placed in the release metadata. This does NOT implement
HTTPS, copying an ISO to MayFS, installation, or A/B boot slot switching.

Uses OpenSSL 3 CLI Ed25519 pkeyutl -sign -rawin. The owner is responsible
for keeping the PRIVATE KEY offline and embedding its matching PUBLIC key
in trusted MayOS builds. Never use CI demo keys for a public release.
"""
from __future__ import annotations

import argparse
from datetime import date
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile


MAX_MANIFEST = 2048
MAX_IMAGE = 0xFFFFFFFF  # current C MayKernel manifest size_bytes uses u32
VERSION_RE = re.compile(r"(?:0|[0-9]{1,7})\.(?:0|[0-9]{1,7})\.(?:0|[0-9]{1,7})(?:-pre[0-9]{1,7}(?:-dev)?)?\Z")


def checked_version(value: str) -> str:
    if not VERSION_RE.fullmatch(value) or len(value) > 32:
        raise ValueError("version must match MayKernel format such as 0.5.0-pre3, max 32 ASCII chars")
    return value


def checked_changelog(value: str) -> str:
    if not 1 <= len(value) <= 48 or any(ord(c) < 32 or ord(c) > 126 for c in value):
        raise ValueError("changelog must contain 1–48 printable ASCII characters, no line breaks")
    return value


def digest_and_length(path: Path) -> tuple[str, int]:
    hashed = hashlib.sha256()
    count = 0
    with path.open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            count += len(block)
            if count > MAX_IMAGE:
                raise ValueError("image exceeds 4 GiB MayKernel u32 metadata limit")
            hashed.update(block)
    if count == 0:
        raise ValueError("refusing zero-byte image")
    return hashed.hexdigest(), count


def manifest_bytes(*, image: Path, version: str, released: str, eta: int, changelog: str) -> bytes:
    checked_version(version)
    checked_changelog(changelog)
    if date.fromisoformat(released).isoformat() != released:
        raise ValueError("release_date must be exact YYYY-MM-DD")
    if not 2023 <= int(released[:4]) <= 9999:
        raise ValueError("release_date outside MayKernel range")
    if not 1 <= eta <= 1440:
        raise ValueError("eta_minutes must be 1–1440")
    sha, size = digest_and_length(image)
    # Ordering, spelling, casing and terminating newline are intentionally
    # exact: signature covers these bytes as-is, including LF characters.
    fields = (
        "name=MayOS\n"
        f"version={version}\n"
        "architecture=x86_64\n"
        "kernel=MayKernel\n"
        f"release_date={released}\n"
        f"size_bytes={size}\n"
        f"sha256={sha}\n"
        f"eta_minutes={eta}\n"
        f"changelog={changelog}\n"
    )
    result = fields.encode("ascii")
    if not 1 <= len(result) <= MAX_MANIFEST:
        raise ValueError("manifest exceeds Rust/C 2048-byte gate")
    return result


def openssl(*args: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(["openssl", *args], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def produce(args: argparse.Namespace) -> tuple[Path, Path]:
    image = args.image.expanduser().resolve(strict=True)
    secret = args.private_key.expanduser().resolve(strict=True)
    output = args.output_dir.expanduser().resolve()
    if not image.is_file() or not secret.is_file():
        raise ValueError("image and signing key must be regular existing files")
    if output == image.parent or output == secret.parent:
        raise ValueError("use a dedicated output directory, not the image/key directory")
    if output.exists() and any(output.iterdir()):
        raise ValueError("output directory must be empty to avoid mixing old and new signed releases")
    contents = manifest_bytes(
        image=image, version=args.version, released=args.date,
        eta=args.eta_minutes, changelog=args.changelog,
    )
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-sign-", dir=output) as temporary:
        tmp = Path(temporary)
        raw = tmp / "Update.may"
        signature = tmp / "Update.sig"
        derived_pub = tmp / "verify.pub.pem"
        raw.write_bytes(contents)
        # Ed25519 signs message bytes, NOT a prehashed SHA-256 digest. The
        # signed manifest contains the image SHA-256 in its authenticated data.
        openssl("pkeyutl", "-sign", "-rawin", "-inkey", str(secret),
                "-in", str(raw), "-out", str(signature))
        if signature.stat().st_size != 64:
            raise ValueError("unexpected Ed25519 signature length; refusing output")
        openssl("pkey", "-in", str(secret), "-pubout", "-out", str(derived_pub))
        # Verify the exact exported bytes before exposing signed artifacts.
        openssl("pkeyutl", "-verify", "-rawin", "-pubin",
                "-inkey", str(derived_pub), "-in", str(raw),
                "-sigfile", str(signature))
        # The manifest and signature are published into an otherwise empty
        # directory; the private key and public key are NEVER copied there.
        final_manifest = output / "Update.may"
        final_signature = output / "Update.sig"
        os.replace(raw, final_manifest)
        os.replace(signature, final_signature)
    return final_manifest, final_signature


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--date", required=True)
    parser.add_argument("--eta-minutes", type=int, required=True)
    parser.add_argument("--changelog", required=True)
    args = parser.parse_args()
    try:
        raw, sig = produce(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"MayOS signed release packaging FAILED: {error}\n")
    print(f"PASS: exact Ed25519-signed MayOS release manifest: {raw}")
    print(f"PASS: detached 64-byte signature: {sig}")
    print("NO INSTALL: these files alone do not authenticate network transport or boot an ISO")
    print("NOTE: current MayFS Update.bin is limited to 4096 bytes; full ISO install not yet implemented")


if __name__ == "__main__":
    main()

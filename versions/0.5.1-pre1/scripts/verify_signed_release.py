#!/usr/bin/env python3
"""Offline verifier of signed MayOS metadata against a SEPARATELY trusted key.

The trusted Ed25519 public key must be supplied explicitly from outside the
downloaded Release directory. Verifies exact raw Update.may signature with
OpenSSL, checks canonical MayKernel metadata, and streams the complete ISO
hash and length. Read-only: never writes partitions or authorizes install.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess

from package_signed_release import manifest_bytes


REQUIRED_KEYS = (
    "name", "version", "architecture", "kernel", "release_date",
    "size_bytes", "sha256", "eta_minutes", "changelog",
)


def verified_metadata(*, manifest: Path, signature: Path, image: Path,
                      public_key: Path) -> bytes:
    manifest = manifest.expanduser().resolve(strict=True)
    signature = signature.expanduser().resolve(strict=True)
    image = image.expanduser().resolve(strict=True)
    public_key = public_key.expanduser().resolve(strict=True)
    if public_key.parent in {manifest.parent, signature.parent}:
        raise ValueError("trusted public key must be outside the untrusted release directory")
    if any(not p.is_file() for p in (manifest, signature, image, public_key)):
        raise ValueError("all inputs must be readable regular files")
    if manifest.stat().st_size == 0 or manifest.stat().st_size > 2048:
        raise ValueError("invalid MayKernel manifest size")
    if signature.stat().st_size != 64:
        raise ValueError("Ed25519 signature must be exactly 64 bytes")

    try:
        subprocess.run(
            ["openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
             "-inkey", str(public_key), "-in", str(manifest),
             "-sigfile", str(signature)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
        )
    except subprocess.CalledProcessError as error:
        raise ValueError("detached Ed25519 signature rejected by trusted release key") from error

    raw = manifest.read_bytes()
    try:
        # Re-serialize canonical producer format; unlike a loose dictionary
        # parser this rejects duplicate keys, reordering and injected controls.
        lines = raw.decode("ascii").splitlines(keepends=True)
    except UnicodeDecodeError as error:
        raise ValueError("signed manifest is not ASCII") from error
    if len(lines) != len(REQUIRED_KEYS) or any(not line.endswith("\n") for line in lines):
        raise ValueError("unexpected MayKernel manifest lines")
    fields: dict[str, str] = {}
    for line, key in zip(lines, REQUIRED_KEYS):
        if not line.startswith(key + "=") or line.count("=") != 1:
            raise ValueError(f"invalid or repeated MayKernel key: {key}")
        fields[key] = line[len(key) + 1:-1]
    if fields["name"] != "MayOS" or fields["kernel"] != "MayKernel" or fields["architecture"] != "x86_64":
        raise ValueError("wrong product, kernel or architecture")
    if not re.fullmatch(r"[0-9]+", fields["size_bytes"]) or not re.fullmatch(r"[0-9]+", fields["eta_minutes"]):
        raise ValueError("invalid numeric metadata")
    expected = manifest_bytes(
        image=image,
        version=fields["version"],
        released=fields["release_date"],
        eta=int(fields["eta_minutes"]),
        changelog=fields["changelog"],
    )
    if raw != expected:
        raise ValueError("signed manifest image size/hash or canonical format does not match")
    return raw


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--image", required=True, type=Path)
    p.add_argument("--manifest", required=True, type=Path)
    p.add_argument("--signature", required=True, type=Path)
    p.add_argument("--trusted-public-key", required=True, type=Path)
    args = p.parse_args()
    try:
        raw = verified_metadata(
            image=args.image, manifest=args.manifest, signature=args.signature,
            public_key=args.trusted_public_key,
        )
    except (ValueError, OSError, subprocess.CalledProcessError) as err:
        p.exit(1, f"MayOS release verification FAILED: {err}\n")
    print(f"PASS: signed MayOS Release manifest and image digest verified ({len(raw)} metadata bytes)")
    print("NO INSTALL: verified files must still pass supported TLS, rollback and A/B installer gates")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Offline MayOS release manifest/signature packager (NOT an installer).

Signer stays off the CI runners/repository. Only manifest, detached Ed25519
signature, and optional public-key fingerprint are emitted. The full image
is NEVER copied into MayFS: MayKernel's current local file cap is 4096 B.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
from datetime import date

VERSION = re.compile(r"^[0-9]{1,7}\.[0-9]{1,7}\.[0-9]{1,7}(?:-pre[0-9]{1,7}(?:-dev)?)?$")
PUB_DER_PREFIX = bytes.fromhex("302a300506032b6570032100")

def sign(key: Path, manifest: bytes) -> tuple[bytes, bytes]:
    # Read public key from the signer, never copy or print the secret PEM.
    exported = subprocess.run(
        ["openssl", "pkey", "-in", str(key), "-pubout", "-outform", "DER"],
        check=True, capture_output=True
    ).stdout
    if len(exported) != 44 or not exported.startswith(PUB_DER_PREFIX):
        raise ValueError("Signing key is not an Ed25519 private key")
    public_key = exported[len(PUB_DER_PREFIX):]
    with tempfile.TemporaryDirectory(prefix="mayos-signer-") as temp:
        data = Path(temp) / "manifest"
        signature = Path(temp) / "signature"
        data.write_bytes(manifest)
        subprocess.run(
            ["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(key),
             "-in", str(data), "-out", str(signature)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
        )
        signature_bytes = signature.read_bytes()
    if len(signature_bytes) != 64:
        raise ValueError("Ed25519 signing operation did not produce a 64-byte signature")
    return signature_bytes, public_key

def make_manifest(image: Path, version: str, release_date: str,
                  minutes: int, note: str) -> bytes:
    if not VERSION.fullmatch(version):
        raise ValueError("Invalid MayOS version; expected 0.5.0-pre3 / 1.0.0")
    fields = version.split("-")[0].split(".")
    if any(int(part) > 1000000 for part in fields):
        raise ValueError("Version field exceeds kernel parser bound")
    if "-pre" in version:
        pre = version.split("-pre", 1)[1].split("-dev", 1)[0]
        if int(pre) > 1000000:
            raise ValueError("Pre-release field exceeds kernel parser bound")
    if len(release_date) != 10 or date.fromisoformat(release_date).isoformat() != release_date:
        raise ValueError("release-date must be a valid YYYY-MM-DD calendar date")
    if int(release_date[:4]) < 2023:
        raise ValueError("Kernel parser rejects release dates before 2023")
    if minutes < 1 or minutes > 1440:
        raise ValueError("eta-minutes must be 1..1440")
    if not (1 <= len(note) <= 48) or not note.isascii() or any(ord(c) < 32 or ord(c) > 126 for c in note):
        raise ValueError("changelog must contain 1..48 printable ASCII chars")
    if not image.is_file():
        raise ValueError("Image not found")
    length = image.stat().st_size
    if length <= 0 or length > 0xffffffff:
        raise ValueError("Image size must fit MayKernel unsigned 32-bit manifest field")
    h = hashlib.sha256()
    with image.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    fields = {
        "name": "MayOS",
        "version": version,
        "architecture": "x86_64",
        "kernel": "MayKernel",
        "release_date": release_date,
        "size_bytes": str(length),
        "sha256": h.hexdigest(),
        "eta_minutes": str(minutes),
        "changelog": note,
    }
    raw = "".join(f"{key}={value}\n" for key, value in fields.items()).encode("ascii")
    if len(raw) > 2048 or any(len(line) >= 160 for line in raw.splitlines()):
        raise ValueError("Generated manifest exceeds MayKernel safety limit")
    return raw

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path, help="existing binary or ISO to hash")
    parser.add_argument("--key", required=True, type=Path, help="locally-held Ed25519 PRIVATE PEM (never uploaded)")
    parser.add_argument("--output", required=True, type=Path, help="directory for Update.may and Update.sig")
    parser.add_argument("--version", required=True)
    parser.add_argument("--release-date", default=date.today().isoformat())
    parser.add_argument("--eta-minutes", type=int, default=5)
    parser.add_argument("--changelog", default="MayOS signed update preview")
    parser.add_argument("--force", action="store_true", help="replace existing output files")
    args = parser.parse_args()
    output = args.output.resolve()
    image = args.image.resolve()
    secret = args.key.resolve()
    if not secret.is_file():
        parser.error("Signer PEM does not exist. Never paste a private key into chat or CI.")
    paths = (output / "Update.may", output / "Update.sig")
    if any(path.resolve() in (image, secret) for path in paths):
        parser.error("Refusing to overwrite image or signing key")
    if any(path.exists() for path in paths) and not args.force:
        parser.error("Output manifest/signature exists; use --force explicitly")
    try:
        manifest = make_manifest(image, args.version, args.release_date,
                                 args.eta_minutes, args.changelog)
        signature, public_key = sign(secret, manifest)
    except (ValueError, subprocess.CalledProcessError, OSError) as exc:
        parser.error(f"Signing failed; no output installed: {exc}")
    output.mkdir(parents=True, exist_ok=True)
    # Atomic replacement of each file; publishing a matched pair still
    # requires release management and independent verification.
    with tempfile.TemporaryDirectory(dir=output, prefix=".mayos-sign.") as stage:
        tmp_manifest, tmp_signature = (Path(stage) / "Update.may", Path(stage) / "Update.sig")
        tmp_manifest.write_bytes(manifest)
        tmp_signature.write_bytes(signature)
        os.replace(tmp_manifest, paths[0])
        os.replace(tmp_signature, paths[1])
    print("PASS: Ed25519 detached signature over exact Update.may bytes")
    print("Public key (owner must independently pin this in a trusted MayKernel build): " + public_key.hex())
    print("Image SHA-256: " + hashlib.sha256(image.read_bytes()).hexdigest())
    print("IMPORTANT: generated artifacts are NOT an install package; no HTTPS or boot writes")

if __name__ == "__main__":
    main()

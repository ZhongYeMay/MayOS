#!/usr/bin/env python3
"""Convert an OWNER-PINNED Ed25519 release public key to a C include.
No private key is read, generated, stored, or transferred by this tool.
This is a trust-root *build configuration*, not an install authorization.
"""
import argparse
import os
from pathlib import Path

RFC8032_TEST_KEY = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--public-key", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    value = args.public_key.lower()
    if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        parser.error("Ed25519 release public key must be exactly 32 bytes (64 hex digits)")
    if value == "00"*32 or value == "ff"*32:
        parser.error("invalid placeholder key: refuse release signature feature")
    if value == RFC8032_TEST_KEY and os.environ.get("MAYOS_ALLOW_TEST_RELEASE_KEY") != "1":
        parser.error("publicly known RFC 8032 test signing key cannot authenticate releases")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    numbers = ",".join("0x" + value[i:i+2] for i in range(0,64,2))
    args.output.write_text(
        "/* Generated from OWNER-PINNED public key. DO NOT embed private keys. */\n"
        "static const u8 mayos_release_root_key[32] = {" + numbers + "};\n",
        encoding="ascii"
    )
    print("PASS: pinned 32-byte public key incorporated; no signing secret handled")

if __name__ == "__main__":
    main()

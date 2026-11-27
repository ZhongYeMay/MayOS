#!/usr/bin/env python3
"""Sign a live MayKernel challenge using an operator's time-authority key."""
import argparse
from pathlib import Path
import re
import time

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import load_pem_private_key


def signature(key, challenge, epoch):
    return key.sign(b"MAYOS-UTC-V1" + bytes.fromhex(challenge) + epoch.to_bytes(8, "big"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", required=True, type=Path)
    parser.add_argument("challenge")
    args = parser.parse_args()
    if not re.fullmatch("[0-9a-f]{64}", args.challenge):
        parser.error("expected the 64-character lowercase challenge")
    key = load_pem_private_key(args.key.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        parser.error("expected an Ed25519 authority private key")
    # Use only on the operator's time authority with a trustworthy UTC clock.
    epoch = int(time.time())
    signed = signature(key, args.challenge, epoch).hex()
    print(f"tls time-begin {epoch}")
    for offset in range(0, len(signed), 48):
        print("tls time-part " + signed[offset:offset + 48])
    print("tls time-apply")


if __name__ == "__main__":
    main()

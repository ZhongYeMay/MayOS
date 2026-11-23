#!/usr/bin/env python3
"""Set a build's operator-approved time-authority trust root."""
import os
from pathlib import Path
import re
import sys

key = os.environ.get("MAYOS_TIME_PUBKEY_HEX", "").lower()
if not re.fullmatch("[0-9a-f]{64}", key) or len(set(bytes.fromhex(key))) == 1:
    raise SystemExit("MAYOS_TIME_PUBKEY_HEX must contain a real 32-byte Ed25519 public key")
if key == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a":
    raise SystemExit("Refusing the published RFC 8032 test key")
data = "static const unsigned char mayos_time_key[] = {" + ", ".join(
    f"0x{key[i:i+2]}" for i in range(0, len(key), 2)) + "};\n"
target = Path(sys.argv[1])
if not target.exists() or target.read_text() != data:
    target.write_text(data)

#!/usr/bin/env python3
"""Fetch and verify the published MayOS Linux64 Beta 4 installer payload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
from urllib.request import urlopen

RELEASE = json.loads(r'''{
  "commit": "7377e253021f6b5a614e3462e994933604c2317f",
  "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz",
  "bytes": 515242230,
  "sha256": "b8a92293fabe887b4c6b43fbae0444453eeb4a0d4517655805c8c730eb3555c3",
  "parts": [
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part01",
      "bytes": 67108864,
      "sha256": "89a54df5ae6c446a1a237d34fc544b9924abb31ca0723a896727c47d9c1d3167"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part02",
      "bytes": 67108864,
      "sha256": "563ab761872a373f4964db24ec172bf5619eac88bb2384635379cc20f56890e6"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part03",
      "bytes": 67108864,
      "sha256": "0910adcdac745f66c1972467473d14b6f8482a138c2aad73364dc0333f7aff58"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part04",
      "bytes": 67108864,
      "sha256": "033f6920b5eec42abd6088c047115bf47f28be57776b54c514b73fec900e611a"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part05",
      "bytes": 67108864,
      "sha256": "6389caa429d2c53b247d6859668b39c71744963d1c49f9e097b1edca920ea850"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part06",
      "bytes": 67108864,
      "sha256": "5b6f4804ce15a310a4f940a06036bdbd87c32311f7749e30d44d43918b1745df"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part07",
      "bytes": 67108864,
      "sha256": "75dc2bb035d3fb3fd13a72ddf724902e18a22804f104054fc7938a5ad45037e7"
    },
    {
      "name": "MayOS-0.5.4-beta.4-Linux64-Root.img.gz.part08",
      "bytes": 45480182,
      "sha256": "d61ea2dd7ab447fc068da60703ad17a6cee1e2bf5e9651755830169134ae904f"
    }
  ]
}''')
PINNED_ROOT = "https://raw.githubusercontent.com/ZhongYeMay/MayOS/" + RELEASE["commit"]
CHUNK = 1024 * 1024


def append_part(part, output):
    start = output.tell()
    for attempt in range(3):
        checksum = hashlib.sha256()
        remaining = part["bytes"]
        try:
            with urlopen(PINNED_ROOT + "/" + part["name"], timeout=90) as response:
                while remaining:
                    chunk = response.read(min(remaining, CHUNK))
                    if not chunk:
                        raise OSError("download ended early")
                    remaining -= len(chunk)
                    checksum.update(chunk)
                    output.write(chunk)
                if response.read(1) or checksum.hexdigest() != part["sha256"]:
                    raise OSError("downloaded part failed SHA-256 verification")
            return
        except OSError:
            output.seek(start)
            output.truncate()
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(RELEASE["name"]))
    args = parser.parse_args()
    destination = args.output.absolute()
    if destination.exists() or destination.is_symlink():
        parser.exit(1, f"Output already exists: {destination}\n")
    try:
        with tempfile.TemporaryDirectory(prefix=".mayos-linux64-", dir=destination.parent) as scratch:
            pending = Path(scratch) / "root.img.gz"
            with pending.open("w+b") as output:
                for index, part in enumerate(RELEASE["parts"], 1):
                    print(f"Downloading part {index}/{len(RELEASE['parts'])}...", flush=True)
                    append_part(part, output)
                if output.tell() != RELEASE["bytes"]:
                    raise OSError("assembled file has an unexpected size")
                output.flush()
                output.seek(0)
                checksum = hashlib.sha256()
                for chunk in iter(lambda: output.read(CHUNK), b""):
                    checksum.update(chunk)
                if checksum.hexdigest() != RELEASE["sha256"]:
                    raise OSError("assembled file failed SHA-256 verification")
                os.fsync(output.fileno())
            os.link(pending, destination)
    except OSError as error:
        parser.exit(1, f"MayOS download failed: {error}\n")
    print(f"Verified SHA-256 {RELEASE['sha256']}\nSaved: {destination}")


if __name__ == "__main__":
    main()

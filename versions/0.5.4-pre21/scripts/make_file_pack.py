#!/usr/bin/env python3
"""Bundle up to 16 small files for explicit import into MayOS."""
import argparse
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    if len(args.files) > 16:
        parser.error("at most 16 files per pack")
    names = set()
    pieces = [b"MAYPACK1", struct.pack("<I", len(args.files))]
    for path in args.files:
        name = path.name.encode("utf-8")
        if not (0 < len(name) < 48) or name in names or not path.is_file():
            parser.error(f"invalid or duplicate file: {path}")
        if path.stat().st_size > 32767:
            parser.error(f"file exceeds 32767 bytes: {path}")
        data = path.read_bytes()
        if len(data) > 32767:
            parser.error(f"file exceeds 32767 bytes: {path}")
        names.add(name)
        pieces.append(struct.pack("<48sI", name, len(data)))
        pieces.append(data)
    with args.output.open("xb") as output:
        output.write(b"".join(pieces))


if __name__ == "__main__":
    main()

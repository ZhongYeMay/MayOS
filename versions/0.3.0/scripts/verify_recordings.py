#!/usr/bin/env python3
"""Verify that both real QEMU recordings belong to one exact build."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("--release", required=True)
parser.add_argument("--commit", required=True)
parser.add_argument("metadata", nargs=2, type=Path)
args = parser.parse_args()
assert re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-pre[1-9][0-9]*)?", args.release)
assert re.fullmatch(r"[0-9a-f]{40}", args.commit)

records = [json.loads(path.read_text(encoding="utf-8")) for path in args.metadata]
assert {record["kernel"] for record in records} == {"custom", "linux"}
assert {record["firmware"] for record in records} == {"uefi"}
assert {record["mayos_release"] for record in records} == {args.release}
assert {record["source_commit"] for record in records} == {args.commit}
assert len({record["iso_sha256"] for record in records}) == 1
for metadata, record in zip(args.metadata, records):
    video = metadata.with_name(record["video"])
    assert video.is_file() and video.stat().st_size > 0
    assert sha256(video) == record["video_sha256"]
    assert record["frames"] >= 100 and record["duration_seconds"] >= 10
    assert record["capture"].startswith("Real-time QEMU framebuffer")
print("PASS: two UEFI recordings share one ISO, version and source commit")

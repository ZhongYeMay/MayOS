#!/usr/bin/env python3
"""Reject incomplete, fabricated, stale or mismatched QEMU feature-tour videos."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

REQUIRED = {
    "UEFI/GRUB/dual-boot",
    "Desktop/Windows pointer/Dock",
    "System clock",
    "Network quick settings",
    "Notification center",
    "Power menu",
    "Settings shortcut",
    "App launcher/search",
    "Browser HTTP/HTML",
    "Browser bookmarks",
    "Browser clickable links",
    "Browser navigation/history/scroll",
    "Browser TLS safety",
    "File manager",
    "Terminal/system info/network diagnostics",
    "Settings/theme",
    "Window overview/Alt-Tab",
    "Desktop context menu",
    "File editor/save/persistence",
}


def sha(path):
    with path.open("rb") as reader:
        return hashlib.file_digest(reader, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("metadata", nargs=2, type=Path)
    args = parser.parse_args()
    records = []
    for path in args.metadata:
        data = json.loads(path.read_text())
        video = path.with_name(data["video"])
        assert video.is_file() and video.stat().st_size >= 20000, video
        assert data["video_sha256"] == sha(video), video
        assert data["iso_sha256"] == sha(args.iso), args.iso
        assert data["mayos_release"] == "0.4.0-pre3", data
        assert data["source_commit"] == args.commit, data
        assert data["firmware"] == "uefi", data
        assert data["frames"] >= 400 and data["duration_seconds"] >= 40, data
        assert data["capture"].startswith("Real-time QEMU framebuffer"), data
        assert data["http_requests_observed"] >= 2, data
        covered = {item["component"] for item in data["coverage"]}
        assert REQUIRED <= covered, sorted(REQUIRED - covered)
        assert {"MayKernel network" if data["kernel"] == "custom" else "Linux network"} <= covered
        details = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=codec_name,width,height,pix_fmt",
            "-of", "json", str(video),
        ], text=True))
        stream = details["streams"][0]
        assert stream["codec_name"] == "h264", stream
        assert (stream["width"], stream["height"]) == (1024, 768), stream
        records.append(data)
        print(f"PASS: {data['kernel']} {len(covered)} features and "
              f"{data['duration_seconds']:.1f}s of real QEMU video")

    assert {r["kernel"] for r in records} == {"custom", "linux"}
    assert len({r["iso_sha256"] for r in records}) == 1
    print("PASS: both full-feature videos match exact verified pre3 ISO and source")


if __name__ == "__main__":
    main()

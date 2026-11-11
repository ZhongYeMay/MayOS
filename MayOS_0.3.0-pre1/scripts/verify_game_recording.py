#!/usr/bin/env python3
"""Fail CI when QEMU game recording evidence is absent or invalid."""
import argparse
import json
import subprocess
from pathlib import Path


def verify(video: Path, metadata: Path, minimum_frames: int) -> None:
    if not video.is_file() or video.stat().st_size < 4096:
        raise ValueError(f"missing or implausibly small recording: {video}")
    if not metadata.is_file():
        raise ValueError(f"missing recording metadata: {metadata}")
    data = json.loads(metadata.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not data:
        raise ValueError("recording metadata is empty")
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-count_frames", "-show_entries",
         "stream=codec_name,width,height,nb_read_frames",
         "-of", "json", str(video)],
        capture_output=True, text=True, check=True,
    )
    streams = json.loads(probe.stdout).get("streams", [])
    if not streams:
        raise ValueError("recording has no video stream")
    stream = streams[0]
    frames = int(stream.get("nb_read_frames", 0))
    if frames < minimum_frames:
        raise ValueError(f"only {frames} decoded frames; need {minimum_frames}")
    if int(stream.get("width", 0)) < 640 or int(stream.get("height", 0)) < 480:
        raise ValueError("recording resolution below 640x480")
    print(f"QEMU recording evidence: {frames} frames, "
          f"{stream['width']}x{stream['height']}, "
          f"{stream.get('codec_name', 'unknown')}; metadata present")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--minimum-frames", type=int, default=20)
    args = parser.parse_args()
    verify(args.video, args.metadata, args.minimum_frames)

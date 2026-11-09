#!/usr/bin/env python3
"""Record a REAL MayKernel game UI session inside QEMU (never an imitation video).

Does not claim that DeepSeek works on MayKernel. A successful recording proves
BIOS + GRUB + MayKernel desktop + standalone game UI + keyboard and MayFS draft.
Every frame is captured from the VM framebuffer through QMP.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
from record_boot import BUILD, Session, sha

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso",type=Path,default=BUILD/"mayos.iso")
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--source-commit",required=True)
    parser.add_argument("--qemu",default="qemu-system-x86_64")
    parser.add_argument("--fps",type=int,default=10)
    options=parser.parse_args()
    if not re.fullmatch("[0-9a-f]{40}",options.source_commit):
        parser.error("full source commit SHA required")
    if not 1<=options.fps<=30:parser.error("invalid frame rate")
    options.iso=options.iso.resolve()
    options.output=options.output.resolve()
    if not options.iso.is_file():parser.error("Build MayKernel ISO first")
    options.firmware="bios"
    options.kernel="custom"
    options.release="0.4.0-pre37"
    options.ovmf_code=None
    options.ovmf_vars=None
    start=datetime.now(timezone.utc).isoformat()
    iso_sha=sha(options.iso)
    session=Session(options)
    try:
        session.start_recording()
        session.wait("MayOS: desktop ready",timeout=120)
        session.hold(2)
        # Click actual new MayKernel app icon on the left navigation rail.
        session.click(52,610)
        session.wait("native Summer game launched",timeout=20)
        session.hold(2)
        session.command("HELLO FROM MAYKERNEL")
        session.hold(2)
        session.key("f5")
        session.hold(2)
        session.key("down")
        session.hold(2)
        session.key("esc")
        session.hold(2)
        session.finish_recording()
    finally:
        session.close()
    if sha(options.iso)!=iso_sha:raise RuntimeError("ISO mutated while recording")
    result={
      "recorded_at_utc":start,
      "source_commit":options.source_commit,
      "iso_sha256":iso_sha,
      "video_sha256":sha(options.output),
      "frames":session.frames,
      "fps":options.fps,
      "duration_seconds":session.frames/options.fps,
      "guest":"MayKernel native x86 freestanding game UI",
      "capture":"QEMU framebuffer via QMP; no simulated or synthesized gameplay frames",
      "proof":["GRUB boot","native MayKernel desktop","native game UI opened from dock",
               "free text entry","draft save key","native heroine selection"],
      "limitations":["MayKernel TLS and DeepSeek runtime not implemented",
                     "original WebP artwork not decoded in MayKernel",
                     "no background audio driver"],
      "qemu":subprocess.check_output([options.qemu,"--version"],text=True).splitlines()[0]
    }
    options.output.with_suffix(".json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=="__main__":
    main()

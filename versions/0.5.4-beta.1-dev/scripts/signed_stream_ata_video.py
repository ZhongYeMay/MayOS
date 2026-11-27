#!/usr/bin/env python3
"""Real QEMU framebuffer MP4 of isolated signed 64KiB ATA staging.

Every image is captured from the LIVE x86_64 MayKernel guest through QMP.
The private Ed25519 signing key is never copied into or recorded in the VM.
This shows an OFFLINE QEMU test, not real HTTPS installation.
"""
from __future__ import annotations
import argparse
import shutil
import subprocess
import time
from pathlib import Path
from PIL import Image
from signed_stage_e2e_smoke import make_disk
from smoke import VM, BUILD

FRAMES=BUILD/"signed-ata-video-frames"
OUTPUT=BUILD/"MayOS-signed-stream-to-ATA-real-QEMU.mp4"

class RecordingVM(VM):
    frames=0
    def capture(self):
        ppm=FRAMES/"capture.ppm"
        self.q("human-monitor-command",{"command-line":"screendump "+str(ppm)})
        frame=FRAMES/(f"{self.frames:04d}.jpg")
        Image.open(ppm).convert("RGB").save(frame,"JPEG",quality=90)
        ppm.unlink(missing_ok=True)
        self.frames+=1
    def key(self,name):
        super().key(name)
        self.capture()

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--package-dir",type=Path,required=True)
    args=p.parse_args()
    if FRAMES.exists():shutil.rmtree(FRAMES)
    FRAMES.mkdir(parents=True)
    manifest=(args.package_dir/"Update.may").read_bytes()
    sig=(args.package_dir/"Update.sig").read_bytes()
    disk=make_disk(BUILD/"signed-ata-video-disk.img",manifest,sig)
    vm=RecordingVM(disk,"signed-ata-video")
    try:
        vm.wait("MayOS: MayFS mounted")
        vm.capture()
        vm.key("f1")
        vm.cmd("update auth-stream-stage-test")
        vm.wait("MayOS: signed 64KiB stream staged to isolated ATA and rehashed PASS; NO INSTALL")
        vm.wait("Signed stream-to-ATA QEMU diagnostic PASS (NO INSTALL)")
        for _ in range(20):
            vm.capture()
            time.sleep(.05)
    finally:
        vm.stop()
    subprocess.run([
        "ffmpeg","-y","-hide_banner","-loglevel","error",
        "-framerate","8","-start_number","0","-i",str(FRAMES/"%04d.jpg"),
        "-c:v","libx264","-preset","veryfast","-crf","23",
        "-pix_fmt","yuv420p","-movflags","+faststart",str(OUTPUT)
    ],check=True)
    assert OUTPUT.stat().st_size>10000,"no real QEMU MP4 produced"
    print(f"PASS: recorded {vm.frames} live QEMU framebuffer frames to real MP4; NO INSTALL",flush=True)

if __name__=="__main__":
    main()

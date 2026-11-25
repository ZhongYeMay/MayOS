#!/usr/bin/env python3
"""Real QEMU framebuffer video of the Rust+C native stream integrity test.

No synthetic UI: every frame is a QMP screendump of the booted MayKernel.
The video demonstrates the actual typed MayShell command and visible outcome.
"""
import shutil
import subprocess
import time
from pathlib import Path
from PIL import Image
from smoke import VM, BUILD

FRAMES=BUILD/"stream-integrity-video-frames"
OUTPUT=BUILD/"MayOS-stream-integrity-native-QEMU.mp4"

class RecordingVM(VM):
    frame_index=0

    def capture(self):
        ppm=FRAMES/"live.ppm"
        self.q("human-monitor-command",
               {"command-line":"screendump "+str(ppm)})
        frame=FRAMES/("%04d.jpg" % RecordingVM.frame_index)
        Image.open(ppm).convert("RGB").save(frame,"JPEG",quality=90)
        ppm.unlink(missing_ok=True)
        RecordingVM.frame_index+=1

    def key(self,key_name):
        super().key(key_name)
        self.capture()

def main():
    if FRAMES.exists():
        shutil.rmtree(FRAMES)
    FRAMES.mkdir(parents=True)
    RecordingVM.frame_index=0
    vm=RecordingVM(None,"maykernel-stream-integrity-video")
    try:
        vm.capture()
        vm.key("f1")
        vm.cmd("tls digest-test")
        vm.wait("MayOS: HTTP stream digest self-test PASSED (NO DOWNLOAD)")
        vm.wait("Native Bing HTTPS prerequisites: pending")
        # Real QEMU frames: hold the final real terminal state for readability.
        for _ in range(18):
            vm.capture()
            time.sleep(.04)
    finally:
        vm.stop()
    subprocess.run([
        "ffmpeg","-y","-hide_banner","-loglevel","error",
        "-framerate","8","-start_number","0",
        "-i",str(FRAMES/"%04d.jpg"),
        "-c:v","libx264","-preset","veryfast","-crf","22",
        "-pix_fmt","yuv420p","-movflags","+faststart",
        str(OUTPUT)
    ],check=True)
    assert OUTPUT.stat().st_size>10000, "QEMU capture video missing"
    print("PASS: real x86_64 Rust+C MayKernel QEMU MP4 captured: "+
          str(RecordingVM.frame_index)+" real framebuffer frames",flush=True)

if __name__=="__main__":
    main()

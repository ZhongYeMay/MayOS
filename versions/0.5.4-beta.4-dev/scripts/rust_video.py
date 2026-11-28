#!/usr/bin/env python3
"""Record ACTUAL QEMU framebuffer frames from the Rust+C native MayKernel.
Produces an MP4 evidence video by encoding QMP screendumps, never a mock UI.
The video contains two isolated MayFS scenarios: accepted untrusted metadata
and a duplicate-field manifest that Rust refuses.
"""
import shutil
import subprocess
import time
from pathlib import Path
from PIL import Image
from smoke import VM, BUILD
from rust_smoke import MANIFEST, write_image


class RecordingVM(VM):
    frames = BUILD / "rust-c-qa-video-frames"
    number = 0

    def __init__(self, disk, tag):
        super().__init__(disk, tag)
        self.capture()

    def capture(self):
        source = self.frames / "live.ppm"
        self.q("human-monitor-command",
               {"command-line": "screendump " + str(source)})
        time.sleep(.05)
        output = self.frames / ("%04d.jpg" % RecordingVM.number)
        Image.open(source).convert("RGB").save(output, "JPEG", quality=91)
        source.unlink(missing_ok=True)
        RecordingVM.number += 1

    def key(self, name):
        super().key(name)
        self.capture()


def film():
    if RecordingVM.frames.exists():
        shutil.rmtree(RecordingVM.frames)
    RecordingVM.frames.mkdir(parents=True)
    RecordingVM.number = 0
    for marker, manifest, expected in [
        ("accepted", MANIFEST, "MayOS: Rust no_std manifest gate passed (UNTRUSTED)"),
        ("rejected", MANIFEST + b"name=MayOS\n",
         "MayOS: Rust update manifest gate rejected untrusted metadata"),
    ]:
        disk = BUILD / ("rust-video-" + marker + ".img")
        write_image(disk, manifest)
        vm = RecordingVM(disk, "rust-video-" + marker)
        try:
            vm.key("f1")
            vm.cmd("update check")
            vm.wait(expected)
            vm.capture()
            vm.cmd("update status")
            vm.capture()
            # Capture only real QEMU pixels; hold final frame for readability.
            for _ in range(12):
                vm.capture()
        finally:
            vm.stop()

    output = BUILD / "MayOS-Rust-C-native-QEMU-test.mp4"
    subprocess.run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-framerate", "8", "-start_number", "0",
        "-i", str(RecordingVM.frames / "%04d.jpg"),
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "21",
        "-pix_fmt", "yuv420p", "-movflags", "+faststart",
        str(output),
    ], check=True)
    assert output.stat().st_size > 10000, "No real-video frames encoded"
    print("PASS: real native Rust+C MayKernel QEMU video captured: "
          + str(output) + " (" + str(RecordingVM.number)
          + " true QEMU framebuffer frames)", flush=True)


if __name__ == "__main__":
    film()

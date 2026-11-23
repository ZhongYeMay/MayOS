#!/usr/bin/env python3
"""Record the real QEMU framebuffer with USB-only input and NVMe diagnostics."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time

from record_boot import Session, sha


def timestamp(seconds):
    milliseconds = round(seconds * 1000)
    hours, milliseconds = divmod(milliseconds, 3600000)
    minutes, milliseconds = divmod(milliseconds, 60000)
    seconds, milliseconds = divmod(milliseconds, 1000)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d},{milliseconds:03d}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    parser.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    args.iso = args.iso.resolve()
    args.machine = "q35,i8042=off"
    args.firmware, args.kernel, args.fps = "uefi", "xhci-nvme", 12
    with tempfile.TemporaryDirectory(prefix="mayos-hardware-video-") as directory:
        root = Path(directory)
        nvme = root / "nvme.img"
        with nvme.open("wb") as handle:
            handle.truncate(8 * 1024 * 1024)
        original = sha(nvme)
        args.output = root / "capture.mp4"
        args.extra_qemu_args = (
            "-device", "qemu-xhci,id=usb0", "-device", "qemu-xhci,id=usb1",
            "-device", "usb-kbd,bus=usb0.0", "-device", "usb-mouse,bus=usb1.0",
            "-drive", f"file={nvme},format=raw,if=none,id=testnvme",
            "-device", "nvme,drive=testnvme,serial=MAYOSVIDEO")
        vm = Session(args)
        chapters = [(0, "OVMF UEFI 启动，测试机配置为两组 xHCI 控制器和 NVMe")]
        def mark(label):
            chapters.append((time.monotonic() - vm.capture_started, label))
        try:
            vm.start_recording()
            vm.wait("Test hardware (RAM only)", timeout=30)
            vm.hold(0.6)
            mark("选择 Test hardware (RAM only)，仅在内存中试用")
            vm.key("down")
            vm.key("ret")
            vm.wait("hardware trial uses RAM only; disk writes disabled", timeout=25)
            vm.wait("xHCI uses a 64-bit high MMIO BAR")
            vm.wait("NVMe uses a 64-bit high MMIO BAR")
            vm.wait("USB boot keyboard ready")
            vm.wait("USB boot mouse ready")
            vm.hold(1.5)
            mark("USB 键盘打开 MayShell，hardware 显示控制器和 NVMe 命名空间")
            vm.key("f1")
            vm.command("hardware")
            vm.wait("Storage: RAM-only hardware trial")
            vm.hold(3)
            mark("USB 鼠标打开文件管理器，USB 键盘提交文件名")
            vm.click(539, 674)
            vm.click(479, 212)
            vm.command("hardware-demo.txt")
            vm.hold(1.5)
            vm.key("f1")
            vm.command("ls Documents")
            vm.wait("\n       hardware-demo.txt\n")
            vm.hold(1.6)
            mark("目录中的文件仅存在于本次内存会话")
            vm.hold(3.0)
            vm.finish_recording()
            end = vm.frames / args.fps
        finally:
            vm.close()
        assert sha(nvme) == original

        captions = root / "captions.srt"
        captions.write_text("".join(
            f"{i + 1}\n{timestamp(start)} --> {timestamp(finish)}\n{text}\n\n"
            for i, ((start, text), finish) in enumerate(zip(
                chapters, [start for start, _ in chapters[1:]] + [end]))),
            encoding="utf-8")
        top = root / "title.txt"
        top.write_text("MayOS 0.5.2-pre3  /  QEMU 虚拟机实录  /  PS/2 已禁用", encoding="utf-8")
        graph = (
            "scale=1120:630,pad=1280:720:80:44:color=0x10131b,"
            f"drawtext=font='Noto Sans CJK SC':textfile={top}:fontsize=21:"
            "fontcolor=0xf2f4fa:x=80:y=9,"
            f"subtitles={captions}:force_style='FontName=Noto Sans CJK SC,FontSize=16,"
            "PrimaryColour=&H00F4F4F4,Outline=0,Shadow=0,MarginV=5'")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(args.output), "-vf", graph,
                        "-an", "-c:v", "libx264", "-crf", "19", "-pix_fmt", "yuv420p",
                        "-movflags", "+faststart", str(output)], check=True)
        print(output)


if __name__ == "__main__":
    main()

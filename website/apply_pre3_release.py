#!/usr/bin/env python3
"""Publish only verified MayOS pre3 release assets onto the static website.

Invoked after a successful v0.4.0-pre3 GitHub Release. Preserves all historic
pre1/pre2 videos and download links rather than overwriting old media.
"""
from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import shutil

BASE = Path(__file__).resolve().parent
VERSION = "0.4.0-pre3"
TAG = "v" + VERSION
PREFIX = f"MayOS-{VERSION}"
RELEASE_URL = f"https://github.com/ZhongYeMay/MayOS/releases/download/{TAG}/"


def replace(src: str, old: str, new: str, label: str) -> str:
    if old not in src:
        raise RuntimeError(f"{label}: expected original fragment absent: {old[:110]}")
    return src.replace(old, new)


def read_hashes(path: Path) -> dict[str, str]:
    results = {}
    for line in path.read_text().splitlines():
        digest, filename = line.strip().split(maxsplit=1)
        filename = filename.removeprefix("*")
        results[filename] = digest
    return results


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", required=True, type=Path)
    args = parser.parse_args()
    assets = args.assets.resolve()
    checksums = read_hashes(assets / "SHA256SUMS.txt")
    required = [
        f"{PREFIX}-dual-boot.iso",
        f"{PREFIX}-MayKernel.iso",
        f"{PREFIX}-all-features-MayKernel.mp4",
        f"{PREFIX}-all-features-Linux.mp4",
    ]
    for name in required:
        path = assets / name
        if not path.is_file():
            raise RuntimeError(f"Missing actual verified release asset: {name}")
        if sha256(path.read_bytes()).hexdigest() != checksums[name]:
            raise RuntimeError(f"Corrupt release asset: {name}")
    recordings = {}
    for name, kernel in [("MayKernel", "custom"), ("Linux", "linux")]:
        stem = f"{PREFIX}-all-features-{name}"
        meta = json.loads((assets / (stem + ".json")).read_text())
        assert meta["mayos_release"] == VERSION and meta["kernel"] == kernel
        assert meta["video_sha256"] == checksums[stem + ".mp4"]
        assert meta["frames"] >= 400 and meta["duration_seconds"] >= 40
        assert len(meta.get("coverage", [])) >= 18
        recordings[name] = meta
    assert recordings["MayKernel"]["source_commit"] == recordings["Linux"]["source_commit"]
    assert recordings["MayKernel"]["iso_sha256"] == recordings["Linux"]["iso_sha256"]
    assert recordings["MayKernel"]["iso_sha256"] == checksums[f"{PREFIX}-dual-boot.iso"]

    media = BASE / "media"
    media.mkdir(exist_ok=True)
    for name in ("MayKernel", "Linux"):
        stem = f"{PREFIX}-all-features-{name}"
        for suffix in (".mp4", ".json"):
            shutil.copy2(assets / (stem + suffix), media / (stem + suffix))
    shutil.copy2(assets / f"{PREFIX}-browser.png", media / f"{PREFIX}-browser.png")

    # Homepage: use real QEMU screenshot and link to the tested GitHub release.
    path = BASE / "index.html"
    home = path.read_text()
    home = replace(home, "0.4.0-pre2", VERSION, "homepage version")
    home = replace(home, "MayOS-0.4.0-pre3-desktop.png",
                   f"{PREFIX}-browser.png", "homepage screenshot")
    remote = RELEASE_URL + f"{PREFIX}-browser.png"
    home = replace(home, remote, "media/" + f"{PREFIX}-browser.png",
                   "homepage screenshot local source")
    home = home.replace("双核启动录像", "双内核交互实录")
    path.write_text(home)

    # Downloads: two authentic ISO assets + hashes, sources and historic versions.
    path = BASE / "download.html"
    dl = path.read_text()
    dl = replace(dl, "0.4.0-pre2", VERSION, "downloads version")
    dual_size = (assets / f"{PREFIX}-dual-boot.iso").stat().st_size / (1024 * 1024)
    native_size = (assets / f"{PREFIX}-MayKernel.iso").stat().st_size / (1024 * 1024)
    dl = replace(dl, "33.4 MB", f"{dual_size:.1f} MB", "dual ISO size")
    dl = replace(dl, "23.9 MB", f"{native_size:.1f} MB", "native ISO size")
    historical = '<a class="btn secondary" href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.0-pre1">'
    dl = replace(
        dl, historical,
        '<a class="btn secondary" href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.0-pre2">0.4.0-pre2</a>' + historical,
        "older preview links")
    path.write_text(dl)

    # Demos: two actual full-feature videos via the website and direct GitHub mirrors.
    path = BASE / "demos.html"
    demos = path.read_text()
    demos = replace(demos, "0.4.0-pre2", VERSION, "demos version")
    demos = replace(demos, "MayOS-0.4.0-pre3-UEFI-", f"{PREFIX}-all-features-",
                    "full feature MP4s")
    demos = replace(demos, f"{PREFIX}-desktop.png", f"{PREFIX}-browser.png",
                    "demos screenshot")
    demos = replace(demos, RELEASE_URL + f"{PREFIX}-browser.png",
                    "media/" + f"{PREFIX}-browser.png",
                    "demos poster screenshot")
    demos = replace(demos, f"{PREFIX}-windows.png", f"{PREFIX}-browser.png",
                    "demos linux poster")
    # Other two gallery screenshots intentionally remain historic pre2 assets.
    for suffix in ("status-panel", "browser"):
        bogus = RELEASE_URL + f"{PREFIX}-{suffix}.png"
        # Newly built browser screenshot is local; old status panel still pre2.
        if suffix == "status-panel" and bogus in demos:
            demos = demos.replace(
                bogus,
                "https://github.com/ZhongYeMay/MayOS/releases/download/"
                "v0.4.0-pre2/MayOS-0.4.0-pre2-status-panel.png")
    old = "32.7 秒"
    demos = replace(demos, old,
                    f'{recordings["MayKernel"]["duration_seconds"]:.1f} 秒',
                    "MayKernel recording runtime")
    old = "35.5 秒"
    demos = replace(demos, old,
                    f'{recordings["Linux"]["duration_seconds"]:.1f} 秒',
                    "Linux recording runtime")
    demos = replace(demos, "UEFI 固件、GRUB、自研内核、Material 3 桌面、系统面板与实际输入。",
                    "真实 UEFI 启动与桌面、浏览器标签页、HTTP 链接、文件编辑、系统设置和网络操作。",
                    "MayKernel tour copy")
    demos = replace(demos, "从同一双内核镜像进入 Linux 图形桌面，展示共享界面与日常系统交互。",
                    "从同一双内核 ISO 进入 Linux，逐项验证浏览器、窗口、终端、文件与系统面板。",
                    "Linux tour copy")
    history = '<div class="actions"><a class="link" href="media/MayOS-0.3.0-pre2-UEFI-MayKernel.mp4">'
    demos = replace(
        demos, history,
        '<div class="actions">'
        '<a class="link" href="media/MayOS-0.4.0-pre2-UEFI-MayKernel.mp4">MayKernel 0.4.0-pre2</a>'
        '<a class="link" href="media/MayOS-0.4.0-pre2-UEFI-Linux.mp4">Linux 0.4.0-pre2</a>'
        '<a class="link" href="media/MayOS-0.3.0-pre2-UEFI-MayKernel.mp4">',
        "historic recordings")
    path.write_text(demos)

    # Verify only the currently advertised recording, but keep pre2 and pre1.
    path = BASE / "check_site.py"
    check = path.read_text()
    check = replace(check, 'CURRENT = "0.4.0-pre2"',
                    'CURRENT = "0.4.0-pre3"', "site version gate")
    check = replace(check, 'PREFIX = "MayOS-0.4.0-pre2-UEFI-"',
                    'PREFIX = "MayOS-0.4.0-pre3-all-features-"',
                    "site video gate")
    check = replace(check, "MayOS-0.4.0-pre2-dual-boot.iso",
                    f"{PREFIX}-dual-boot.iso", "site dual ISO gate")
    check = replace(check, "MayOS-0.4.0-pre2-MayKernel.iso",
                    f"{PREFIX}-MayKernel.iso", "site native ISO gate")
    path.write_text(check)
    print("PASS: verified 0.4.0-pre3 website homepage, ISOs and real H.264 videos")


if __name__ == "__main__":
    main()

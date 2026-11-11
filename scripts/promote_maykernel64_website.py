#!/usr/bin/env python3
"""Promote the verified MayKernel64 GitHub Release on the GitHub Pages website.

Only called AFTER the matching GitHub Release exists and has verified ISO files.
Historical pre3 videos and screenshots keep their original release metadata.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "website"
VERSION = "0.4.1-pre1"
BASE = "0.4.0-pre3"


def change(path: Path, old: str, new: str) -> None:
    source = path.read_text(encoding="utf-8")
    if old not in source:
        raise ValueError(f"{path.name}: expected website text not found: {old[:80]}")
    path.write_text(source.replace(old, new), encoding="utf-8")


index = WEB / "index.html"
change(index, "MayOS 0.4.0-pre3 双内核桌面系统", "MayOS 0.4.1-pre1 原生64位双内核桌面系统")
change(index, "MayOS 0.4.0-pre3 / QEMU-verified", "MayOS 0.4.1-pre1 / x86_64 QEMU-verified")
change(index, "<h2>MayOS<br>0.4.0-pre3</h2>", "<h2>MayOS<br>0.4.1-pre1</h2>")
change(index,
       'href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.0-pre3">查看 GitHub Release',
       'href="https://github.com/ZhongYeMay/MayOS/releases/tag/v0.4.1-pre1">查看 GitHub Release')
change(index, "<strong>v0.4.0-pre3</strong>", "<strong>v0.4.1-pre1</strong>")

download = WEB / "download.html"
change(download, BASE, VERSION)
change(download, f"MayOS-{VERSION}-dual-boot.iso", f"MayOS-{VERSION}-dual-boot-x86_64.iso")
change(download, f"MayOS-{VERSION}-MayKernel.iso", f"MayOS-{VERSION}-MayKernel-x86_64.iso")
change(download, f"MayOS-{VERSION}-source.tar.gz", f"MayOS-{VERSION}-source.zip")
change(download, "MayKernel x86 和 Linux", "MayKernel x86_64 和 Linux")
change(download, "QEMU x86 / x86_64", "QEMU x86_64")
change(download, '<span class="chip">31.9 MB</span>', '<span class="chip">x86_64 BIOS / UEFI</span>')
change(download, '<span class="chip">22.8 MB</span>', '<span class="chip">Native ELF64</span>')
change(download, '<span class="chip">x86</span>', '<span class="chip">x86_64</span>')

check = WEB / "check_site.py"
change(check, 'CURRENT = "0.4.0-pre3"', 'CURRENT = "0.4.1-pre1"\nDEMO_RELEASE = "0.4.0-pre3"')
change(check, 'for name in ("index.html", "download.html", "demos.html"):\n    assert CURRENT in docs[name][1], (name, "stale release")\n    assert "0.3.0-pre2" not in docs[name][1] or name == "demos.html", name',
       'for name in ("index.html", "download.html"):\n    assert CURRENT in docs[name][1], (name, "stale release")\n    assert "0.3.0-pre2" not in docs[name][1], name\nassert DEMO_RELEASE in docs["demos.html"][1], "historical demo missing"')
change(check,
       'assert "MayOS-0.4.0-pre3-dual-boot.iso" in docs["download.html"][1]',
       'assert "MayOS-0.4.1-pre1-dual-boot-x86_64.iso" in docs["download.html"][1]')
change(check,
       'assert "MayOS-0.4.0-pre3-MayKernel.iso" in docs["download.html"][1]',
       'assert "MayOS-0.4.1-pre1-MayKernel-x86_64.iso" in docs["download.html"][1]')
change(check,
       'assert record["mayos_release"] == CURRENT',
       'assert record["mayos_release"] == DEMO_RELEASE')
change(check,
       'print("PASS: MayOS 0.4.0-pre2 website navigation and real MP4 hashes")',
       'print("PASS: MayOS 0.4.1-pre1 website and historical pre3 media hashes")')

print(f"Prepared website downloads for MayOS {VERSION}; legacy videos unchanged.")

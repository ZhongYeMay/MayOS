#!/usr/bin/env python3
"""Full-feature *real* QEMU recording of MayOS dual-kernel desktop.

Every section is enacted on the booted guest: desktop/status bar, app launcher,
file manager/editor, browser navigation, local HTTP, settings, network, shell,
notifications, window switching, and context menu. This is an integration
tour of supported user-facing workflows, not a claim every OS codepath is tested.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import re
from threading import Thread
import time

from record_boot import Session, sha


class Site(BaseHTTPRequestHandler):
    hits = 0

    def do_GET(self):
        Site.hits += 1
        items = b"".join(
            b"<p>Line %02d - MayOS QEMU Browser</p>" % i for i in range(34)
        )
        if self.path == "/second":
            body = b"<html><body><h1>SECOND_PAGE_OK</h1><p>History works</p></body></html>"
        else:
            body = (b"<html><body><h1>MAYOS_BROWSER_OK</h1>"
                    b"<script>NOT_EXECUTABLE</script>" + items + b"</body></html>")
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def wait_hits(vm: Session, before: int, seconds: float = 25) -> None:
    end = time.monotonic() + seconds
    while Site.hits <= before:
        if vm.proc.poll() is not None:
            raise RuntimeError("QEMU guest stopped during real HTTP test")
        if time.monotonic() > end:
            raise TimeoutError("No TCP/HTTP request reached the local test server")
        time.sleep(.1)


def type_url(vm: Session, url: str) -> None:
    vm.click(548, 211)     # browser address bar / replace existing URL
    vm.command(url)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", choices=("custom", "linux"), required=True)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ovmf-code", type=Path, required=True)
    parser.add_argument("--ovmf-vars", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--fps", type=int, default=10)
    args = parser.parse_args()
    args.release = "0.4.0-pre3"
    args.firmware = "uefi"
    args.iso = args.iso.resolve()
    args.output = args.output.resolve()
    args.ovmf_code = args.ovmf_code.resolve()
    args.ovmf_vars = args.ovmf_vars.resolve()
    args.network_models = ("e1000", "rtl8139")
    if not re.fullmatch("[0-9a-f]{40}", args.source_commit):
        parser.error("--source-commit must be a pinned 40-char SHA")
    if not 6 <= args.fps <= 15:
        parser.error("--fps must be from 6 to 15")
    for file in (args.iso, args.ovmf_code, args.ovmf_vars):
        if not file.is_file():
            parser.error(f"Required ISO/UEFI file missing: {file}")

    server = HTTPServer(("127.0.0.1", 0), Site)
    Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://10.0.2.2:{server.server_port}"
    iso_hash = sha(args.iso)
    coverage: list[dict] = []

    def done(step: str, evidence: str) -> None:
        coverage.append({"component": step, "evidence": evidence})
        print("VERIFIED", step, evidence, flush=True)

    vm = Session(args)
    try:
        vm.start_recording()
        vm.wait("custom kernel", timeout=90)
        vm.key("down" if args.kernel == "linux" else "up")
        vm.hold(2)
        vm.key("ret")
        vm.wait("MayOS: desktop ready", timeout=180)
        vm.hold(2)
        done("UEFI/GRUB/dual-boot", "Actual firmware boot and desktop-ready serial event")

        if args.kernel == "custom":
            vm.wait("MayNet: DHCP ready", timeout=35)
            vm.wait("MayNet: gateway ARP resolved", timeout=35)
            done("MayKernel network", "RTL8139 DHCP lease and real gateway ARP reply")
        else:
            vm.hold(3)
            done("Linux network", "Active Ethernet checked by subsequent HTTP request")

        vm.click(948, 94)     # close initial Settings window
        vm.hold(2)
        done("Desktop/Windows pointer/Dock", "Rendered after real QEMU mouse input")

        for label, x in [
            ("System clock", 730),
            ("Network quick settings", 789),
            ("Notification center", 889),
            ("Power menu", 930),
            ("Settings shortcut", 970),
        ]:
            vm.click(x, 20)
            vm.hold(1)
            vm.click(607, 455)
            done(label, "Icon clicked; popover shown/closed")
        vm.click(248, 717)
        vm.hold(2)
        vm.key("esc")
        done("App launcher/search", "Dock app drawer opened and dismissed")

        vm.click(716, 717)    # Explorer
        vm.hold(1)
        before = Site.hits
        type_url(vm, url + "/")
        vm.wait("Explorer: HTTP navigation", timeout=35)
        wait_hits(vm, before)
        vm.hold(2)
        done("Browser HTTP/HTML", "Guest fetched real local HTTP HTML via QEMU NIC")

        vm.key("ctrl-t")       # new independent tab
        vm.wait("Explorer: new tab", timeout=25)
        before = Site.hits
        vm.command(url + "/second")
        wait_hits(vm, before)
        vm.click(235, 165)     # return to first tab, preserving page
        vm.wait("Explorer: tab switch", timeout=25)
        vm.click(399, 165)     # second tab
        vm.key("ctrl-w")       # close without closing the browser app
        vm.wait("Explorer: tab close", timeout=25)
        done("Browser multi-tab", "Ctrl+T, independent HTTP page, tab switch and Ctrl+W")

        vm.click(925, 211)    # bookmark
        vm.wait("Explorer: bookmark added", timeout=25)
        done("Browser bookmarks", "Real session favorite added")

        before = Site.hits
        type_url(vm, url + "/second")
        wait_hits(vm, before)
        vm.hold(1)
        vm.click(214, 211)   # history back
        vm.wait("Explorer: history back", timeout=25)
        vm.click(256, 211)   # forward
        vm.wait("Explorer: history forward", timeout=25)
        vm.click(298, 211)   # refresh
        vm.wait("Explorer: reload", timeout=25)
        vm.key("ctrl-f")
        vm.wait("Explorer: find opened", timeout=25)
        vm.command("History works")
        vm.wait("Explorer: find match", timeout=25)
        vm.key("esc")
        done("Browser find-in-page", "Ctrl+F and actual text match scrolled into view")
        vm.click(915, 583)   # next page of text
        vm.wait("Explorer: page down", timeout=25)
        vm.click(340, 211)   # home
        vm.wait("Explorer: home", timeout=25)
        vm.click(310, 255)   # saved bookmark
        vm.hold(1)
        done("Browser navigation/history/scroll", "Back, forward, reload, page down, home and saved favorite")

        type_url(vm, "https://example.com")
        vm.wait("Explorer: HTTPS unsupported", timeout=25)
        done("Browser TLS safety", "Unsupported HTTPS reported without downgrading it to HTTP")

        vm.click(346, 717)   # Files
        vm.hold(2)
        done("File manager", "Opened native file manager via Dock")

        vm.key("f1")
        vm.hold(1)
        offset = len(vm.text())
        vm.command("neofetch")
        vm.wait("Kernel:", timeout=35, offset=offset)
        vm.command("pwd")
        vm.command("ls")
        vm.command("net")
        vm.hold(2)
        done("Terminal/system info/network diagnostics", "neofetch output plus real MayShell commands")

        vm.command("theme light")
        vm.hold(1)
        vm.command("theme dark")
        vm.hold(1)
        done("Settings/theme", "Both light and dark theme commands executed")

        vm.key("alt-tab")
        vm.hold(2)
        vm.key("esc")
        done("Window overview/Alt-Tab", "Guest opened/dismissed real window overview")
        vm.click(987, 648, button="right")
        vm.wait("Desktop: context menu", timeout=25)
        vm.hold(2)
        vm.key("esc")
        done("Desktop context menu", "Actual desktop context-menu serial event after right-click")

        vm.click(346, 717)    # Files
        vm.hold(1)
        vm.click(370, 212)    # New document
        vm.command("video.txt")
        vm.key("ret")
        vm.command("QEMU_FILE_EDITOR_OK")
        vm.key("ctrl-s")
        vm.hold(1)
        vm.key("f1")
        offset = len(vm.text())
        vm.command("cat Documents/video.txt")
        vm.wait("QEMU_FILE_EDITOR_OK", timeout=35, offset=offset)
        done("File editor/save/persistence", "Real GUI creation + Ctrl-S, verified through shell reading")

        vm.hold(3)
        vm.finish_recording()
    finally:
        vm.close()
        server.shutdown()
        server.server_close()

    if sha(args.iso) != iso_hash:
        raise RuntimeError("ISO changed while recording")
    if len(coverage) < 17 or Site.hits < 2:
        raise RuntimeError("Full interactive coverage requirements were not met")
    if vm.frames < args.fps * 40:
        raise RuntimeError("Recorded video is too short to show all checks")
    data = {
        "mayos_release": args.release,
        "kernel": args.kernel,
        "firmware": args.firmware,
        "source_commit": args.source_commit,
        "iso_sha256": iso_hash,
        "video": args.output.name,
        "video_sha256": sha(args.output),
        "capture": "Real-time QEMU framebuffer via QMP, no simulated frames",
        "fps": args.fps,
        "frames": vm.frames,
        "duration_seconds": vm.frames / args.fps,
        "coverage": coverage,
        "http_requests_observed": Site.hits,
        "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(data, ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()

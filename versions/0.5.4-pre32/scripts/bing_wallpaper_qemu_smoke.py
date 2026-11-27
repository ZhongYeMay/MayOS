#!/usr/bin/env python3
"""Exercise MayKernel's HTTPS wallpaper flow against an isolated Bing-like server."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
from pathlib import Path
import shutil
import ssl
import subprocess
import tempfile
import threading
import time

from PIL import Image
from dual_boot_smoke import BUILD, BootVM
from roughtime_fixture import Authority
from tls_wire_smoke import certificates

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = "/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=en-US"
COLORS = [(32, 82, 160), (163, 74, 33)]


def jpeg(color, progressive):
    image = BytesIO()
    Image.new("RGB", (1920, 1080), color).save(image, "JPEG", progressive=progressive)
    return image.getvalue()


def wait_for_pixel(vm, name, expected):
    deadline = time.monotonic() + 6
    path = BUILD / (name + ".ppm")
    while time.monotonic() < deadline:
        vm.q("screendump", {"filename": str(path)})
        with Image.open(path) as frame:
            pixel = frame.getpixel((frame.width * 7 // 8, frame.height // 3))
            if max(abs(channel - target) for channel, target in zip(pixel, expected)) <= 4:
                frame.save(BUILD / (name + ".png"))
                path.unlink()
                return
        time.sleep(.1)
    raise AssertionError(f"Expected desktop color {expected}, received {pixel}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bearssl-source", type=Path, required=True)
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    visits = []
    first, second = jpeg(COLORS[0], False), jpeg(COLORS[1], True)

    class Origin(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            if self.headers.get("Host") != "www.bing.com":
                self.send_error(400)
                return
            visits.append(self.path)
            if self.path == ARCHIVE:
                checks = visits.count(ARCHIVE)
                label = "First" if checks <= 2 else "Broken" if checks == 3 else "New"
                day = {"First": "01", "Broken": "02", "New": "03"}[label]
                data = json.dumps({"images": [{"startdate": "202611" + day, "wp": True,
                                               "url": "/th?id=OHR." + label}]}).encode()
                content_type = "application/json"
            elif self.path.startswith("/th?id=OHR."):
                data = {"/th?id=OHR.First": first, "/th?id=OHR.Broken": b"broken",
                        "/th?id=OHR.New": second}[self.path]
                content_type = "image/jpeg"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            self.wfile.flush()
            try:
                self.connection.settimeout(3)
                self.connection.unwrap().close()
            except OSError:
                pass

    with tempfile.TemporaryDirectory(prefix="mayos-bing-native-") as folder, \
            ThreadingHTTPServer(("127.0.0.1", 0), Origin) as server, Authority() as authority:
        work = Path(folder)
        ca, leaf, secret = certificates(work, hostname="www.bing.com")
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(leaf, secret)
        server.socket = context.wrap_socket(server.socket, server_side=True)

        source = work / "source"
        shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns("build", "__pycache__"))
        (source / "build").mkdir()
        key = ", ".join(f"0x{byte:02x}" for byte in authority.public_key)
        (source / "build/roughtime_fixture_key.h").write_text(
            "static const unsigned char server_key[32] = {" + key + "};\n")
        subprocess.run(["python3", str(ROOT / "scripts/generate_tls_x509_fixture.py"),
                        "--ca", str(ca), "--leaf", str(leaf),
                        "--output", str(source / "build/tls_x509_fixture.h")], check=True)
        subprocess.run(["make", "-j4", "all", f"BEARSSL_SOURCE={args.bearssl_source.resolve()}",
                        f"MAYOS_BING_DIAGNOSTIC_PORT={server.server_port}",
                        "MAYOS_ROUGHTIME=1", f"MAYOS_ROUGHTIME_TEST_PORT={authority.port}"],
                       cwd=source, check=True)

        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        disk = work / "disk.img"
        with disk.open("xb") as f:
            f.truncate(8 * 1024 * 1024)
        vm = BootVM(iso=source / "build/mayos.iso", disk=disk, tag="bing-native",
                    qemu=shutil.which("qemu-system-x86_64"), memory=512, timeout=45)
        try:
            vm.wait("MayOS: desktop ready")
            vm.wait("DHCP ACK accepted; lease acquired")
            rejection = "MayOS: Roughtime reply rejected"
            synchronized = "MayOS: signed UTC synchronized over Roughtime"
            vm.wait(rejection)
            assert not visits, "Wallpaper network activity before authenticated UTC"
            vm.wait(synchronized)
            print("PASS: bad time signature rejected and signed UTC obtained automatically", flush=True)
            marker = "MayOS: native Bing daily wallpaper ready"
            vm.wait(marker)
            wait_for_pixel(vm, "bing-native-first", COLORS[0])
            print("PASS: authenticated metadata and baseline JPEG painted the desktop", flush=True)
            check = time.monotonic() + 30
            while "/th?id=OHR.Broken" not in visits and time.monotonic() < check:
                time.sleep(.05)
            assert visits[:5] == [ARCHIVE, "/th?id=OHR.First", ARCHIVE, ARCHIVE, "/th?id=OHR.Broken"], visits
            time.sleep(.5)
            wait_for_pixel(vm, "bing-native-after-invalid-image", COLORS[0])
            print("PASS: unchanged day skipped redownload, invalid image preserved the wallpaper", flush=True)
            vm.wait(marker, offset=vm.text.index(marker) + len(marker))
            wait_for_pixel(vm, "bing-native-after-refresh", COLORS[1])
            print("PASS: new publication date replaced the wallpaper with a progressive JPEG", flush=True)
            log = vm.text
            first_sync = log.index(synchronized)
            renewal_rejected = log.index(rejection, first_sync)
            assert log.index(synchronized, renewal_rejected) > renewal_rejected
            print("PASS: stale signed response rejected and fresh network time renewed", flush=True)
        finally:
            vm.stop()
            server.shutdown()


if __name__ == "__main__":
    main()

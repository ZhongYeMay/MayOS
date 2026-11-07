#!/usr/bin/env python3
"""Browser integration: real HTTP server, QEMU NIC, navigation and text preview.

No external Internet access is needed. Both the HTTP response and click results
come from a booted MayKernel with a real RTL8139 virtual Ethernet adapter.
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from threading import Thread
import time

from PIL import Image, ImageChops
from smoke import BUILD, VM


class Pages(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/second":
            body = b"<html><body><h1>SECOND_PAGE_OK</h1><p>Back and forward</p></body></html>"
        elif self.path == "/long":
            body = b"<html><body>" + b"".join(
                b"<p>Scroll row %03d: MayOS HTTP Preview</p>" % i for i in range(40)
            ) + b"</body></html>"
        else:
            body = (b"<html><body><h1>FIRST_PAGE_OK &amp; Explorer</h1>"
                    b"<script>DO_NOT_RENDER_SCRIPT</script><p>QEMU HTTP works</p></body></html>")
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def count(vm, token):
    return vm.log.read_text(errors="replace").count(token)


def wait_more(vm, token, baseline, timeout=35):
    limit = time.monotonic() + timeout
    while count(vm, token) <= baseline:
        assert vm.proc.poll() is None, "Guest unexpectedly exited"
        assert time.monotonic() < limit, f"Missing new event: {token}\n{vm.log.read_text()}"
        time.sleep(0.08)


def snapshot(vm, name):
    vm.shot(name)
    return Image.open(BUILD / (name + ".png")).convert("RGB")


def type_url(vm, url):
    vm.click(550, 211)  # address bar selects/replaces the URL
    vm.type(url + "\n")


def main():
    server = HTTPServer(("127.0.0.1", 0), Pages)
    Thread(target=server.serve_forever, daemon=True).start()
    disk = BUILD / "browser-integration.img"
    with disk.open("wb") as handle:
        handle.truncate(8 * 1024 * 1024)
    vm = VM(disk, "browser-integration")
    try:
        vm.wait("MayNet: DHCP ready")
        vm.wait("MayNet: gateway ARP resolved")
        vm.click(716, 717)  # Explorer in Material Dock
        home = snapshot(vm, "browser-home")
        url = f"http://10.0.2.2:{server.server_port}"
        previous = count(vm, "MayNet: HTTP response received")
        type_url(vm, url + "/")
        wait_more(vm, "MayNet: HTTP response received", previous)
        first = snapshot(vm, "browser-http-first")
        assert ImageChops.difference(home, first).getbbox(), "HTTP page did not alter display"

        vm.click(925, 211)  # toggle current page bookmark
        vm.wait("Explorer: bookmark added")

        previous = count(vm, "MayNet: HTTP response received")
        type_url(vm, url + "/second")
        wait_more(vm, "MayNet: HTTP response received", previous)
        second = snapshot(vm, "browser-http-second")
        assert ImageChops.difference(first, second).getbbox(), "Second page did not change UI"

        vm.key('ctrl-t')
        vm.wait("Explorer: new tab")
        vm.type(url + '/long\n')
        wait_more(vm, "MayNet: HTTP response received",
                  count(vm, "MayNet: HTTP response received") - 1)
        vm.click(235, 165)
        vm.wait("Explorer: tab switch")
        vm.click(399, 165)
        vm.key('ctrl-w')
        vm.wait("Explorer: tab close")
        snapshot(vm, "browser-material-tabs")

        vm.click(214, 211)   # back
        vm.wait("Explorer: history back")
        vm.click(256, 211)   # forward
        vm.wait("Explorer: history forward")
        vm.click(298, 211)   # reload
        vm.wait("Explorer: reload")

        previous = count(vm, "MayNet: HTTP response received")
        type_url(vm, url + "/long")
        wait_more(vm, "MayNet: HTTP response received", previous)
        vm.key("ctrl-f")
        vm.wait("Explorer: find opened")
        vm.type("Scroll row 015\n")
        vm.wait("Explorer: find match")
        snapshot(vm, "browser-find-in-page")
        vm.key("esc")
        vm.click(916, 583)    # scroll down (actual viewer page)
        vm.wait("Explorer: page down")
        snapshot(vm, "browser-long-page")

        vm.click(340, 211)    # home
        vm.wait("Explorer: home")
        previous = count(vm, "Explorer: HTTP navigation")
        vm.click(310, 255)    # return via first bookmark
        wait_more(vm, "Explorer: HTTP navigation", previous)

        type_url(vm, "https://example.com")
        vm.wait("Explorer: HTTPS unsupported")
        snapshot(vm, "browser-https-disabled")

        print("PASS: QEMU browser HTTP fetch, two pages, history, reload, "
              "bookmark, scrolling, home, and explicit HTTPS unsupported feedback", flush=True)
    finally:
        vm.stop()
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Drive Explorer HTTP and HDA gain through the native desktop."""
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from pathlib import Path
import shutil
import tempfile

from desktop_menu_smoke import application, menu, press, type_text
from dual_boot_smoke import BUILD, BootVM


@contextmanager
def web_server():
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/preview":
                self.send_error(404)
                return
            body = b"MayOS native Explorer preview\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server.server_port
        finally:
            server.shutdown()
            thread.join()


def main():
    qemu = shutil.which("qemu-system-x86_64")
    with web_server() as port, tempfile.TemporaryDirectory(prefix="mayos-platform-") as directory:
        disk = Path(directory) / "data.img"
        with disk.open("xb") as f:
            f.truncate(8 * 1024 * 1024)
        vm = BootVM(iso=BUILD / "mayos.iso", disk=disk, tag="platform-workflow", qemu=qemu,
                    memory=512, timeout=45, extra_args=(
                        "-audiodev", "none,id=audio",
                        "-device", "intel-hda",
                        "-device", "hda-output,audiodev=audio",
                    ))
        try:
            vm.wait("MayOS: desktop ready")
            vm.wait("DHCP ACK accepted")
            menu(vm, 0, 0)
            type_text(vm, "explorer")
            application(vm, "explorer", lambda: press(vm, "ret"))
            vm.keyboard_command(f"http://10.0.2.2:{port}/preview")
            vm.wait("MayOS: Explorer received HTTP response")
            vm.shot("native-explorer")
            application(vm, "settings", lambda: menu(vm, 0, 2))
            menu(vm, 2, 2)
            vm.wait("MayOS: HDA output volume available")
            for x in (540, 700):
                offset = len(vm.text)
                vm.click(x, 304)
                try:
                    vm.wait("MayOS: HDA output volume changed", offset=offset, timeout=3)
                    break
                except TimeoutError:
                    pass
            else:
                raise AssertionError("Settings did not change HDA gain")
            vm.shot("native-audio")
        finally:
            vm.stop()
    print("PASS: MayKernel Explorer fetched an HTTP response and Settings changed hardware HDA output gain")


if __name__ == "__main__":
    main()

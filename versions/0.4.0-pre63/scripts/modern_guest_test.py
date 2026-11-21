#!/usr/bin/env python3
"""Boot the built MayOS ISO and exercise its native HTTPS ELF over QEMU NAT."""
import base64
import http.server
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
from dual_boot_smoke import BootVM, BUILD

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        body = b"MayOS guest TLS transport verified\n"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    cert, key = root / "cert.pem", root / "key.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(key), "-out", str(cert), "-days", "1",
                    "-subj", "/CN=MayOS QA", "-addext", "subjectAltName=IP:10.0.2.2"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    server = http.server.ThreadingHTTPServer(("0.0.0.0", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    disk = BUILD / "test-modern-network-disk.img"
    with disk.open("wb") as stream: stream.truncate(8 * 1024 * 1024)
    vm = None
    try:
        vm = BootVM(iso=BUILD / "mayos-dual.iso", disk=disk, tag="test-modern-network",
                    qemu="qemu-system-x86_64", memory=512, timeout=60)
        vm.wait("custom kernel", timeout=30)
        vm.key("down"); vm.key("ret")
        vm.wait("MayOS-Linux# ", timeout=120)
        vm.wait("MayOS: desktop ready", timeout=120)
        vm.shell_check("/usr/bin/mayos-fetch --capabilities", "MODERN_CAPABILITIES")
        vm.shell_check("test -s /etc/ssl/certs/ca-certificates.crt && test -r /proc/net/if_inet6", "CA_AND_IPV6")
        encoded = base64.b64encode(cert.read_bytes()).decode()
        vm.shell_check(f"printf '%s' '{encoded}' | base64 -d > /tmp/qa-ca.pem", "QA_CA")
        url = f"https://10.0.2.2:{server.server_port}/"
        vm.shell_check("n=0; while ! ip -4 addr show | grep -q '10.0.2.'; do "
                       "n=$((n+1)); test $n -lt 30 || break; sleep 1; done; "
                       "ip route | grep -q default", "NETWORK_ROUTE")
        vm.shell_check(f"MAYOS_CA_BUNDLE=/tmp/qa-ca.pem /usr/bin/mayos-fetch {url} > /tmp/tls.txt && "
                       "grep -q 'MayOS guest TLS transport verified' /tmp/tls.txt", "TLS13_TRUSTED")
        vm.shell_check(f"! /usr/bin/mayos-fetch {url}", "TLS_UNTRUSTED_REJECTED")
        print("PASS: real MayOS guest native ELF + libraries, RTC-valid TLS1.3, CA verification, IPv6 kernel and e1000 NAT")
    finally:
        if vm: vm.stop()
        server.shutdown(); server.server_close()

#!/usr/bin/env python3
"""Real loopback HTTP, IPv6 and TLS tests: no public network required."""
import gzip
import http.server
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import h2.config
import h2.connection
import h2.events

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "build/mayos-fetch"

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *args): pass
    def handle(self):
        try: super().handle()
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError): pass
    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302); self.send_header("Location", "/gzip")
            data = b""
        elif self.path == "/loop":
            self.send_response(302); self.send_header("Location", "/loop")
            data = b""
        elif self.path == "/downgrade":
            self.send_response(302); self.send_header("Location", "http://127.0.0.1:1/")
            data = b""
        elif self.path == "/large":
            self.send_response(200); data = b"x" * 5000
        else:
            self.send_response(200); data = gzip.compress(b"MayOS modern network works")
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try: self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError): pass

def serve(server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server

def fetch(url, ca=None):
    env = os.environ | {"NO_PROXY": "localhost,127.0.0.1,::1", "no_proxy": "localhost,127.0.0.1,::1"}
    if ca: env["MAYOS_CA_BUNDLE"] = str(ca)
    result = subprocess.run([str(CLIENT), url], env=env, text=True, capture_output=True, timeout=12)
    return result.returncode, result.stdout

def test_http2(cert, key):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0)); listener.listen(1)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.load_cert_chain(cert, key)
    context.set_alpn_protocols(["h2"])
    failures = []
    def respond():
        try:
            listener.settimeout(10)
            raw, _ = listener.accept()
            with context.wrap_socket(raw, server_side=True) as connection:
                connection.settimeout(10)
                assert connection.selected_alpn_protocol() == "h2"
                protocol = h2.connection.H2Connection(h2.config.H2Configuration(client_side=False))
                protocol.initiate_connection()
                connection.sendall(protocol.data_to_send())
                while True:
                    data = connection.recv(65536)
                    if not data: break
                    events = protocol.receive_data(data)
                    for event in events:
                        if isinstance(event, h2.events.RequestReceived):
                            protocol.send_headers(event.stream_id, [(":status", "200"), ("content-type", "text/plain")])
                            protocol.send_data(event.stream_id, b"MayOS HTTP2 works", end_stream=True)
                            connection.sendall(protocol.data_to_send())
                            return
                    connection.sendall(protocol.data_to_send())
        except Exception as error: failures.append(error)
    worker = threading.Thread(target=respond, daemon=True); worker.start()
    try:
        code, output = fetch(f"https://localhost:{listener.getsockname()[1]}/", cert)
        worker.join(12)
        assert not worker.is_alive() and not failures, failures
        assert code == 0 and "MayOS HTTP2 works" in output and "Protocol: HTTP/2" in output, output
    finally: listener.close()

with tempfile.TemporaryDirectory() as folder:
    temp = Path(folder)
    key, cert = temp / "key.pem", temp / "cert.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(key), "-out", str(cert), "-days", "1",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    test_http2(cert, key)
    plain = serve(http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler))
    try:
        base = f"http://127.0.0.1:{plain.server_port}"
        code, output = fetch(base + "/redirect")
        assert code == 0 and "MayOS modern network works" in output, output
        assert fetch(base + "/loop")[0] != 0
        code, output = fetch(base + "/large")
        assert code == 0 and "truncated" in output and len(output) < 2048
        for tls in (ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3):
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = context.maximum_version = tls
            context.load_cert_chain(cert, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
            serve(server)
            try:
                url = f"https://localhost:{server.server_port}"
                assert fetch(url, cert)[0] == 0
                assert fetch(url)[0] != 0, "untrusted certificate accepted"
                assert fetch(f"https://127.0.0.1:{server.server_port}", cert)[0] != 0, "wrong hostname accepted"
                assert fetch(url + "/downgrade", cert)[0] != 0, "HTTPS downgrade accepted"
            finally: server.shutdown(); server.server_close()
        class IPv6Server(http.server.ThreadingHTTPServer): address_family = socket.AF_INET6
        ipv6 = serve(IPv6Server(("::1", 0), Handler))
        try: assert fetch(f"http://[::1]:{ipv6.server_port}/")[0] == 0
        finally: ipv6.shutdown(); ipv6.server_close()
        assert fetch("file:///etc/passwd")[0] != 0
        assert fetch("https://user:password@localhost/")[0] != 0
    finally: plain.shutdown(); plain.server_close()
print("PASS: HTTP/2 over TLS 1.3 ALPN, HTTP redirect/gzip/limit, TLS 1.2 + 1.3, trust + hostname checks, downgrade rejection, IPv6")

#!/usr/bin/env python3
"""Build and boot an isolated BearSSL-on-TCP test image with a loopback TLS peer."""
import argparse
import os
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time

from dual_boot_smoke import BUILD, BootVM
from generate_tls_x509_fixture import c_array

ROOT = Path(__file__).resolve().parents[1]
HOSTNAME = "test.mayos.local"
REPLY = b"HTTP/1.0 200 OK\r\nContent-Length: 8\r\n\r\nMAYOS-OK"


def certificates(work):
    ca, leaf = work / "ca.pem", work / "leaf.pem"
    secret = work / "leaf.key"
    def openssl(*args):
        subprocess.run(["openssl", *map(str, args)], check=True, capture_output=True)
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "3", "-subj",
            "/CN=MayOS ephemeral wire test CA", "-keyout", work / "ca.key", "-out", ca,
            "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj",
            f"/CN={HOSTNAME}", "-keyout", secret, "-out", work / "leaf.csr")
    extensions = work / "leaf.ext"
    extensions.write_text(f"basicConstraints=critical,CA:FALSE\n"
                         f"subjectAltName=DNS:{HOSTNAME}\n"
                         "extendedKeyUsage=serverAuth\nkeyUsage=digitalSignature,keyEncipherment\n")
    openssl("x509", "-req", "-in", work / "leaf.csr", "-CA", ca, "-CAkey", work / "ca.key",
            "-CAcreateserial", "-days", "2", "-extfile", extensions, "-out", leaf)
    return ca, leaf, secret


def test_peer(listener, context, events, scenarios):
    for scenario in scenarios:
        try:
            raw, _ = listener.accept()
            with raw:
                raw.settimeout(18)
                with context.wrap_socket(raw, server_side=True) as tls:
                    if scenario not in ("ok", "cpu", "eof"):
                        events.append("invalid certificate accepted")
                        return
                    request = bytearray()
                    while not request.endswith(b"\r\n\r\n") and len(request) < 1024:
                        data = tls.recv(256)
                        if not data:
                            raise RuntimeError("peer closed before HTTP request")
                        request.extend(data)
                    assert request == b"GET /health HTTP/1.0\r\nHost: test.mayos.local\r\n\r\n"
                    tls.sendall(REPLY[:19])
                    if scenario == "eof":
                        os.close(tls.detach())
                        events.append("truncated response")
                        continue
                    time.sleep(.08)
                    tls.sendall(REPLY[19:])
                    tls.unwrap().close()
                    events.append("validated HTTP")
        except ssl.SSLError as exc:
            events.append("handshake refused" if scenario in ("host", "expired", "ca") else str(exc))
        except (OSError, AssertionError, RuntimeError) as exc:
            events.append(str(exc))
            return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bearssl-source", type=Path, required=True)
    args = parser.parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    BUILD.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mayos-wire-tls-") as directory:
        work = Path(directory)
        ca, leaf, secret = certificates(work)
        source = work / "source"
        shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns("build", "__pycache__"))
        (source / "build").mkdir()
        subprocess.run(["python3", str(ROOT / "scripts/generate_tls_x509_fixture.py"), "--ca", str(ca),
                        "--leaf", str(leaf), "--output", str(source / "build/tls_x509_fixture.h")], check=True)
        (source / "build/tls_wire_seed.h").write_text(c_array("mayos_wire_seed", secrets.token_bytes(160)))
        subprocess.run(["make", "-j4", "all", "MAYOS_RUST=1", "MAYOS_TLS_WIRE_DIAGNOSTIC=1",
                        f"BEARSSL_SOURCE={args.bearssl_source.resolve()}"], cwd=source, check=True)

        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(leaf, secret)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(45)
            events = []
            wrapper = work / "qemu"
            wrapper.write_text(f"""#!/usr/bin/env python3
import sys, os
args = sys.argv[1:]
args[args.index('-nic') + 1] = 'user,model=e1000,net=172.31.44.0/24,host=172.31.44.9,dhcpstart=172.31.44.60'
os.execv({qemu!r}, [{qemu!r}] + args)
""")
            wrapper.chmod(0o755)
            disk = work / "isolated.img"
            with disk.open("xb") as stream:
                stream.truncate(8 * 1024 * 1024)
            vm = BootVM(iso=source / "build/mayos.iso", disk=disk, tag="tls-wire",
                        qemu=str(wrapper), memory=512, timeout=42)
            try:
                vm.wait("MayOS: desktop ready")
                vm.wait("DHCP ACK accepted; lease acquired 172.31.44.60")
                cpu_seeded = "HMAC-DRBG seeded from CPU RDSEED" in vm.text
                cases = (
                    ("cpu", "CPU-seeded TLS wire verified HTTP response and TLS close"),

                    ("ok", "TLS wire verified HTTP response and TLS close"),
                    ("host", "TLS wire rejected wrong hostname"),
                    ("expired", "TLS wire rejected expired certificate"),
                    ("ca", "TLS wire rejected untrusted CA"),
                    ("eof", "TLS wire rejected truncated transport"),
                )
                if not cpu_seeded:
                    cases = cases[1:]
                worker = threading.Thread(target=test_peer,
                    args=(listener, context, events, [case for case, _ in cases]), daemon=True)
                worker.start()
                vm.key("f1")
                for case, marker in cases:
                    offset = len(vm.text)
                    vm.keyboard_command(f"tls wire-test {case} {listener.getsockname()[1]}")
                    vm.wait(marker, offset=offset)
                    vm.wait("native TCP connection closed", offset=offset)
                    print(f"PASS: {marker}", flush=True)
                worker.join(timeout=5)
                expected = ["validated HTTP"] * (2 if cpu_seeded else 1)
                assert events == expected + ["handshake refused"] * 3 + ["truncated response"], events
            finally:
                vm.stop()

            vm = BootVM(iso=source / "build/mayos.iso", disk=disk, tag="tls-wire-no-rdseed",
                        qemu=str(wrapper), memory=512, timeout=42, cpu="max,-rdseed")
            try:
                vm.wait("HMAC-DRBG unavailable")
                vm.wait("DHCP ACK accepted; lease acquired 172.31.44.60")
                vm.key("f1")
                offset = len(vm.text)
                vm.keyboard_command(f"tls wire-test cpu {listener.getsockname()[1]}")
                vm.wait("TLS wire diagnostic unavailable", offset=offset)
                expect_no_socket = listener.gettimeout()
                listener.settimeout(2)
                try:
                    peer, _ = listener.accept()
                    peer.close()
                    raise AssertionError("Missing RDSEED must prevent TCP opening")
                except TimeoutError:
                    print("PASS: missing RDSEED prevents TCP opening without fixture entropy", flush=True)
                finally:
                    listener.settimeout(expect_no_socket)
            finally:
                vm.stop()


if __name__ == "__main__":
    main()

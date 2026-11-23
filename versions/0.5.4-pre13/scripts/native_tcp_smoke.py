#!/usr/bin/env python3
"""Test the routed TCP client with an isolated QEMU forwarding fixture."""
import argparse
from pathlib import Path
import shutil
import socket
import struct
import tempfile
import threading
import time

from dual_boot_smoke import BootVM, BUILD


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", choices=("e1000", "rtl8139"), default="e1000")
    parser.add_argument("--ovmf-code", type=Path)
    parser.add_argument("--ovmf-vars", type=Path)
    args = parser.parse_args()
    if bool(args.ovmf_code) != bool(args.ovmf_vars):
        parser.error("supply both OVMF files")
    qemu = shutil.which("qemu-system-x86_64")
    payload = bytes(97 + offset % 26 for offset in range(8191)) + b"\n"
    with tempfile.TemporaryDirectory(prefix="mayos-native-tcp-") as directory, socket.socket() as server:
        root = Path(directory)
        server.bind(("127.0.0.1", 0))
        server.listen()
        server.settimeout(35)
        events = []
        def echo():
            try:
                peer, _ = server.accept()
                with peer:
                    peer.settimeout(10)
                    with peer.makefile("rb") as stream:
                        assert stream.readline() == payload
                        for start in range(0, len(payload), 997):
                            peer.sendall(payload[start:start + 997])
                            time.sleep(.035)
                        assert stream.read(1) == b""
                        events.append("fin")
            except (OSError, AssertionError) as exc:
                events.append(repr(exc))
        worker = threading.Thread(target=echo, daemon=True)
        worker.start()

        disk = root / "isolated.img"
        with disk.open("xb") as file:
            file.truncate(8 * 1024 * 1024)
        wrapper = root / "qemu"
        capture = root / "traffic.pcap"
        backend = ("user,id=tcpqa,net=172.31.44.0/24,host=172.31.44.9,"
                   "dhcpstart=172.31.44.60")
        wrapper.write_text(f"""#!/usr/bin/env python3
import os
import sys
args = sys.argv[1:]
index = args.index('-nic')
args[index:index+2] = ['-netdev', {backend!r},  '-device', {args.model + ',netdev=tcpqa'!r},
                      '-object', {'filter-dump,id=trace,netdev=tcpqa,file=' + str(capture)!r}]
os.execv({qemu!r}, [{qemu!r}] + args)
""")
        wrapper.chmod(0o755)
        variables = None
        if args.ovmf_vars:
            variables = root / "ovmf-vars.fd"
            shutil.copyfile(args.ovmf_vars, variables)
        vm = BootVM(iso=BUILD / "mayos.iso", disk=disk, tag=f"native-tcp-{args.model}",
                    qemu=str(wrapper), memory=512, timeout=55,
                    ovmf_code=args.ovmf_code, ovmf_vars=variables)
        try:
            vm.wait("MayOS: desktop ready")
            vm.wait("DHCP ACK accepted; lease acquired 172.31.44.60")
            vm.key("f1")
            vm.keyboard_command(f"net tcp-echo 172.31.44.9 {server.getsockname()[1]}")
            vm.wait("MayOS: native TCP handshake established")
            vm.wait("MayOS: native TCP echo verified (8192 bytes)")
            vm.wait("MayOS: native TCP connection closed")
            assert events == ["fin"], events
            print("PASS: arbitrary endpoint, 8 KiB streamed in both directions, and FIN", flush=True)

            offset = len(vm.text)
            vm.keyboard_command("net tcp-echo 198.51.100.71 19080")
            try:
                vm.wait("MayOS: native TCP timed out", offset=offset, timeout=7)
            except TimeoutError:
                vm.wait("MayOS: native TCP connection refused", offset=offset, timeout=1)
            offset = len(vm.text)
            vm.keyboard_command("net tcp-echo 172.31.44.250 19080")
            vm.wait("MayOS: native TCP timed out", offset=offset, timeout=13)
            vm.keyboard_command("net status")
            vm.wait("Assigned IPv4:", offset=offset)
        finally:
            vm.stop()

        frames = []
        data = capture.read_bytes()
        position = 24
        while position + 16 <= len(data):
            size = struct.unpack_from("<I", data, position + 8)[0]
            position += 16
            frames.append(data[position:position + size])
            position += size
        leased, router = socket.inet_aton("172.31.44.60"), socket.inet_aton("172.31.44.9")
        remote = socket.inet_aton("198.51.100.71")
        gateway_replies = [frame[22:28] for frame in frames if frame[12:14] == b"\x08\x06"
                           and frame[20:22] == b"\x00\x02" and frame[28:32] == router
                           and frame[38:42] == leased]
        attempts = [frame for frame in frames if frame[12:14] == b"\x08\x00"
                    and frame[23] == 6 and frame[26:30] == leased and frame[30:34] == remote
                    and frame[47] & 0x02]
        assert attempts and gateway_replies
        assert all(frame[:6] == gateway_replies[0] for frame in attempts)
        print("PASS: off-subnet SYN uses the DHCP gateway MAC; unresolved neighbor times out", flush=True)


if __name__ == "__main__":
    main()

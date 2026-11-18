#!/usr/bin/env python3
"""Real MayKernel/QEMU RTL8139 ARP + IPv4 UDP + DNS-A fixture acceptance.

The host loopback server provides deterministic, synthetic packets; this does
NOT exercise public DNS, Internet connectivity, or GitHub updates.
The DHCP handshake below uses QEMU SLIRP's actual DHCP service.
"""
import socket
import threading
from smoke import VM


def fixture_server(sock, shutdown, events):
    sock.settimeout(0.25)
    while not shutdown.is_set():
        try:
            payload, address = sock.recvfrom(512)
        except socket.timeout:
            continue
        except OSError:
            return
        if payload == b"MAYOS-UDP-PROBE":
            sock.sendto(b"MAYOS-UDP-ACK", address)
            events.add("echo")
            continue
        if (len(payload) >= 30 and payload[:2] == b"MA" and
                payload[2:4] == b"\x01\x00" and
                payload[4:6] == b"\x00\x01" and
                payload[12:] == b"\x07example\x04test\x00\x00\x01\x00\x01"):
            # DNS ID 0x4d41, no-error authoritative fixture answer,
            # one question and one A record for example.test -> 203.0.113.7.
            reply = (payload[:2] + b"\x81\x80\x00\x01\x00\x01" +
                     b"\x00\x00\x00\x00" + payload[12:] +
                     b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c" +
                     b"\x00\x04\xcb\x00\x71\x07")
            sock.sendto(reply, address)
            events.add("dns")



def tcp_fixture_server(listener, shutdown, events):
    listener.settimeout(0.25)
    while not shutdown.is_set():
        try:
            connection, _ = listener.accept()
        except socket.timeout:
            continue
        except OSError:
            return
        with connection:
            connection.settimeout(5)
            data = b""
            try:
                while b"\r\n\r\n" not in data and len(data) < 4096:
                    chunk = connection.recv(1024)
                    if not chunk:
                        break
                    data += chunk
                if data.startswith(b"GET /health HTTP/1.1\r\n"):
                    body = b"MAYOS-OK"
                    reply = (b"HTTP/1.1 200 OK\r\nContent-Length: 8\r\n"
                             b"Connection: close\r\n\r\n" + body)
                    connection.sendall(reply)
                    events.add("tcp")
            except OSError:
                continue


def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.bind(("127.0.0.1", 10053))
    shutdown = threading.Event()
    events = set()
    thread = threading.Thread(target=fixture_server,
                              args=(server, shutdown, events), daemon=True)
    thread.start()
    tcp_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp_server.bind(("127.0.0.1", 10080))
    tcp_server.listen(2)
    tcp_thread = threading.Thread(target=tcp_fixture_server,
                                  args=(tcp_server, shutdown, events), daemon=True)
    tcp_thread.start()
    vm = None
    try:
        vm = VM(None, "maykernel-rtl8139-arp", nic=True)
        vm.wait("MayOS: RTL8139 native PCI Ethernet ready")
        vm.key("f1")
        vm.cmd("net status")
        vm.wait("Network: RTL8139 PCI Ethernet ready (polled, L2 only).")
        vm.cmd("net qemu-probe")
        vm.wait("MayOS: RTL8139 QEMU ARP probe sent to 10.0.2.2")
        vm.wait("MayOS: RTL8139 ARP reply received from QEMU gateway 10.0.2.2")
        vm.cmd("net udp-probe")
        vm.wait("MayOS: RTL8139 IPv4 UDP echo probe sent to QEMU host")
        vm.wait("MayOS: RTL8139 IPv4 UDP echo validated from QEMU host")
        vm.cmd("net dns-probe")
        vm.wait("MayOS: RTL8139 DNS fixture request sent over IPv4 UDP")
        vm.wait("MayOS: RTL8139 DNS fixture response validated over IPv4 UDP")
        vm.cmd("net status")
        vm.wait("QEMU IPv4 UDP echo: validated")
        vm.wait("QEMU synthetic DNS A response: validated")
        vm.cmd("net dhcp")
        vm.wait("MayOS: DHCP DISCOVER broadcast sent")
        vm.wait("MayOS: DHCP OFFER received and validated")
        vm.wait("MayOS: DHCP REQUEST broadcast sent")
        vm.wait("MayOS: DHCP ACK accepted; lease acquired")
        vm.cmd("net status")
        vm.wait("DHCP: bound")
        vm.wait("Assigned IPv4:")
        vm.wait("Lease (seconds):")
        vm.cmd("net tcp-probe")
        vm.wait("MayOS: TCP QEMU SYN sent to localhost fixture")
        vm.wait("MayOS: TCP QEMU SYN-ACK validated; handshake established")
        vm.wait("MayOS: TCP QEMU plaintext test request sent")
        vm.wait("MayOS: TCP QEMU HTTP test response validated (plaintext fixture)")
        vm.cmd("net status")
        vm.wait("QEMU TCP: 3-way handshake complete")
        vm.wait("QEMU HTTP: plaintext fixture response validated")
        vm.wait("MayOS: TCP diagnostic FIN acknowledged; clean close")
        vm.cmd("net status")
        vm.wait("QEMU TCP: clean FIN close confirmed")
        vm.shot("maykernel-rtl8139-tcp-plaintext-fixture")
        vm.cmd("net tcp-loss-probe")
        vm.wait("MayOS: TCP diagnostic intentionally dropped first SYN")
        vm.wait("MayOS: TCP diagnostic segment retransmitted")
        vm.wait("MayOS: TCP QEMU SYN-ACK validated; handshake established")
        vm.wait("MayOS: TCP QEMU HTTP test response validated (plaintext fixture)")
        vm.wait("MayOS: TCP diagnostic FIN acknowledged; clean close")
        vm.cmd("net status")
        vm.wait("QEMU TCP retransmissions: 1")
        vm.shot("maykernel-rtl8139-tcp-loss-retransmission")
        assert "echo" in events and "dns" in events and "tcp" in events, events
        print("PASS: real QEMU TCP clean FIN close and intentionally dropped SYN retransmission", flush=True)
    finally:
        if vm is not None:
            vm.stop()
        shutdown.set()
        server.close()
        tcp_server.close()
        thread.join(timeout=2)
        tcp_thread.join(timeout=2)


if __name__ == "__main__":
    main()

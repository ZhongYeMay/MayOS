"""Isolated Roughtime timestamp authority for the QEMU smoke test."""
import hashlib
import socket
import struct
import threading
import time

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

FRAME = b"ROUGHTIM"
DELEGATION = b"RoughTime v1 delegation signature--\0"
RESPONSE = b"RoughTime v1 response signature\0"


def encode(fields):
    ordered = sorted(fields, key=lambda tag: int.from_bytes(tag, "little"))
    payload = b"".join(fields[tag] for tag in ordered)
    offsets, position = [], 0
    for tag in ordered[:-1]:
        position += len(fields[tag])
        offsets.append(position)
    return struct.pack("<" + "I" * len(ordered), len(ordered), *offsets) + b"".join(ordered) + payload


def decode(packet):
    assert packet[:8] == FRAME and struct.unpack_from("<I", packet, 8)[0] == len(packet) - 12
    count = struct.unpack_from("<I", packet, 12)[0]
    head = 12 + count * 8
    ends = [0, *struct.unpack_from("<" + "I" * (count - 1), packet, 16), len(packet) - head]
    return {packet[12 + count * 4 + i * 4:12 + count * 4 + (i + 1) * 4]:
            packet[head + ends[i]:head + ends[i + 1]] for i in range(count)}


class Authority:
    def __init__(self):
        root = Ed25519PrivateKey.generate()
        self.public_key = root.public_key().public_bytes_raw()
        self.online = Ed25519PrivateKey.generate()
        now = int(time.time())
        delegation = encode({b"MINT": struct.pack("<Q", now - 3600),
                             b"MAXT": struct.pack("<Q", now + 3600),
                             b"PUBK": self.online.public_key().public_bytes_raw()})
        self.certificate = encode({b"DELE": delegation, b"SIG\0": root.sign(DELEGATION + delegation)})
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(.2)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        requests = []
        saved = None
        try:
            while not self.stop.is_set():
                try:
                    packet, source = self.socket.recvfrom(1232)
                except TimeoutError:
                    continue
                assert len(packet) == 1024
                fields = decode(packet)
                nonce = fields[b"NONC"]
                assert len(nonce) == 32 and nonce not in requests
                assert fields[b"VER\0"] == struct.pack("<II", 0x8000000b, 0x80000008)
                assert fields[b"SRV\0"] == hashlib.sha512(b"\xff" + self.public_key).digest()[:32]
                requests.append(nonce)

                # Include a sibling to exercise the nonce Merkle proof.
                leaf = hashlib.sha512(b"\0" + nonce).digest()[:32]
                sibling = bytes(range(32))
                proof_root = hashlib.sha512(b"\1" + sibling + leaf).digest()[:32]
                signed = encode({b"MIDP": struct.pack("<Q", int(time.time())),
                                 b"RADI": struct.pack("<I", 1), b"ROOT": proof_root})
                signature = self.online.sign(RESPONSE + signed)
                if len(requests) == 1:
                    signature = bytes((signature[0] ^ 1,)) + signature[1:]
                version = 0x80000008 if len(requests) == 4 else 0x8000000b
                reply = encode({b"CERT": self.certificate, b"INDX": struct.pack("<I", 1),
                                b"NONC": nonce, b"PATH": sibling, b"SIG\0": signature,
                                b"SREP": signed, b"VER\0": struct.pack("<I", version)})
                response = FRAME + struct.pack("<I", len(reply)) + reply
                if len(requests) == 2:
                    saved = response
                self.socket.sendto(saved if len(requests) == 3 else response, source)
        except Exception as error:
            self.error = error

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join(timeout=2)
        self.socket.close()
        if self.error:
            raise self.error

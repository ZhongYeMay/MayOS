#!/usr/bin/env python3
"""Verify native network states against actual QEMU Ethernet devices."""

import argparse
from pathlib import Path
import socket
import time

from dual_boot_smoke import BootVM
from procfs_smoke import command


def status(vm, expected):
    vm.key("f1")
    log = command(vm, "cat /proc/net/mayos", f"\nstate: {expected}\n")
    return log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    args = parser.parse_args()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sink:
        sink.bind(("127.0.0.1", 0))
        port = sink.getsockname()[1]
        for model, backend, expected in (
            (None, None, "no-adapter"),
            ("e1000", f"socket,udp=127.0.0.1:{port},localaddr=127.0.0.1:0", "waiting-ipv4"),
            ("e1000", "user", "ipv4-assigned"),
            ("rtl8139", "user", "ipv4-assigned"),
        ):
            extra = ("-netdev", f"{backend},id=statusnet",
                     "-device", f"{model},netdev=statusnet,id=statusnic") if model else ()
            vm = BootVM(iso=args.iso.resolve(), disk=None, tag=f"network-{model}-{expected}",
                        qemu=args.qemu, memory=512, timeout=45, nic="none", extra_args=extra)
            try:
                vm.wait("MayKernel64 display modes")
                vm.key("ret")
                vm.wait("MayOS: desktop ready")
                if expected == "ipv4-assigned":
                    vm.wait("lease acquired 10.0.2.15")
                status(vm, expected)
                if expected == "ipv4-assigned":
                    vm.wait("\nipv4: 10.0.2.15\n")
                    vm.q("set_link", {"name": "statusnic", "up": False})
                    log = status(vm, "link-down")
                    assert "ipv4:" not in log, log
                    vm.key("f2")
                    vm.wait("MayOS: Task Manager opened")
                    vm.key("up")
                    vm.key("up")
                    vm.wait("MayOS Task Manager: showing Network")
                    time.sleep(1.2)
                    vm.shot(f"network-{model}-link-down")
                    vm.q("set_link", {"name": "statusnic", "up": True})
                    status(vm, "ipv4-assigned")
                    vm.key("f2")
                    time.sleep(1.2)
                    vm.shot(f"network-{model}-ipv4-assigned")
                print(f"PASS: {model or 'no adapter'} {expected}", flush=True)
            finally:
                vm.stop()


if __name__ == "__main__":
    main()

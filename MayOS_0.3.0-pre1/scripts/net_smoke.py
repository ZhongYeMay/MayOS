#!/usr/bin/env python3
"""Exercise native MayKernel RTL8139 TX/RX against real QEMU SLIRP ARP.
No public Internet, TCP stack, installed drivers or host privileges required.
"""
from smoke import VM

def main():
    vm = VM(None, "maykernel-rtl8139-arp", nic=True)
    try:
        vm.wait("MayOS: RTL8139 native PCI Ethernet ready")
        vm.key("f1")
        vm.cmd("net status")
        vm.wait("Network: RTL8139 PCI Ethernet ready (polled, L2 only).")
        vm.cmd("net qemu-probe")
        vm.wait("MayOS: RTL8139 QEMU ARP probe sent to 10.0.2.2")
        vm.wait("MayOS: RTL8139 ARP reply received from QEMU gateway 10.0.2.2")
        vm.cmd("net status")
        vm.wait("QEMU gateway ARP: reply received")
        vm.shot("maykernel-rtl8139-arp-reply")
        print("PASS: real PCI RTL8139, DMA Ethernet TX/RX, QEMU gateway ARP request+reply", flush=True)
    finally:
        vm.stop()

if __name__ == "__main__":
    main()

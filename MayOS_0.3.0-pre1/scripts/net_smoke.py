#!/usr/bin/env python3
"""Exercise actual RTL8139 DHCP, ARP, ICMP and DNS in a booted MayKernel."""
from pathlib import Path
from smoke import BUILD, VM

def main():
    disk = BUILD / 'test-net.img'
    with disk.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    vm = VM(disk, 'test-net')
    try:
        vm.wait('MayNet: RTL8139 ready')
        vm.wait('MayNet: DHCP ready')
        vm.wait('MayNet: gateway ARP resolved')
        vm.key('f1')
        vm.cmd('net')
        vm.wait('MayNet: link up, IPv4 10.0.2.')
        vm.cmd('net ping')
        vm.wait('MayNet: ICMP echo reply')
        vm.cmd('net dns')
        vm.wait('MayNet: DNS A reply')
        vm.shot('maykernel-network')
        print('PASS: RTL8139 PCI, DHCP, ARP, IPv4, ICMP and DNS via QEMU NAT', flush=True)
    finally:
        vm.stop()

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Exercise actual RTL8139 DHCP, ARP, ICMP and DNS in a booted MayKernel."""
from pathlib import Path
from smoke import BUILD, VM
from http.server import BaseHTTPRequestHandler, HTTPServer
from threading import Thread

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        data = b'MayOS-native-http-ok'
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass

def main():
    disk = BUILD / 'test-net.img'
    with disk.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    server = HTTPServer(('127.0.0.1', 0), Handler)
    Thread(target=server.serve_forever, daemon=True).start()
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
        vm.click(716, 717)
        vm.type(f'http://10.0.2.2:{server.server_port}/\n')
        vm.wait('MayNet: HTTP GET sent')
        vm.wait('MayNet: HTTP response received')
        vm.shot('maykernel-http')
        print('PASS: RTL8139 PCI, DHCP, ARP, IPv4, ICMP, DNS and real HTTP/TCP', flush=True)
    finally:
        vm.stop()
        server.shutdown()
        server.server_close()

if __name__ == '__main__':
    main()

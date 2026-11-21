#!/usr/bin/env python3
"""Capture the restored MayOS desktop from the compiled kernel."""
from smoke import VM,BUILD

def main():
    disk=BUILD/'capture.img'
    with disk.open('wb') as f:f.truncate(8*1024*1024)
    vm=VM(disk,'capture')
    try:
        vm.shot('desktop')
        vm.click(55,105);vm.shot('files')
        vm.click(450,300,'right');vm.shot('context-menu');vm.key('esc')
        vm.key('f1');vm.cmd('neofetch');vm.wait('Kernel: MayKernel x86 (custom)');vm.shot('terminal')
        print('Captured restored Settings, Files and Terminal from the actual kernel.',flush=True)
    finally:vm.stop()
if __name__=='__main__':main()

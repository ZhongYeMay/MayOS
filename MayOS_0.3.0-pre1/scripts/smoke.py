#!/usr/bin/env python3
"""Boot the real ISO under QEMU; exercise PS/2 input and ATA persistence."""
import json, os, socket, struct, subprocess, time
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build'
QEMU=os.environ.get('QEMU','qemu-system-i386')
class VM:
    def __init__(self,disk,tag):
        self.sock=BUILD/f'{tag}.sock';self.sock.unlink(missing_ok=True)
        self.log=BUILD/f'{tag}.log'
        args=[QEMU,'-machine','pc','-accel','tcg','-m','128','-vga','std','-display','none','-cdrom',str(BUILD/'mayos.iso'),'-boot','d','-nic','user,model=rtl8139','-serial',f'file:{self.log}','-qmp',f'unix:{self.sock},server=on,wait=off','-no-reboot','-no-shutdown']
        if disk is not None:args+=['-drive',f'file={disk},format=raw,if=ide,index=0']
        if os.environ.get('QEMU_DATA_DIR'):args+=['-L',os.environ['QEMU_DATA_DIR']]
        self.proc=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=(BUILD/f'{tag}-qemu.log').open('w'))
        until=time.monotonic()+20
        while not self.sock.exists():
            assert self.proc.poll() is None,'QEMU failed to start'
            assert time.monotonic()<until,'QMP timed out'
            time.sleep(.05)
        self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(10);self.s.connect(str(self.sock));self.f=self.s.makefile('rwb',buffering=0);self.f.readline();self.q('qmp_capabilities')
        self.wait('MayOS: desktop ready');time.sleep(.5)
        self.mx,self.my=530,390
    def q(self,cmd,args=None):
        self.f.write((json.dumps({'execute':cmd,'arguments':args or {}})+'\n').encode())
        while True:
            r=json.loads(self.f.readline())
            if 'error'in r:raise RuntimeError(r)
            if 'return'in r:return r['return']
    def wait(self,s):
        until=time.monotonic()+20
        while s not in self.log.read_text(errors='replace'):
            assert self.proc.poll() is None,'Guest exited'
            assert time.monotonic()<until,f'Guest timed out waiting for {s!r}:\n{self.log.read_text()}'
            time.sleep(.05)
    def key(self,k):
        self.q('human-monitor-command',{'command-line':f'sendkey {k} 25'});time.sleep(.07)
    def type(self,s):
        names={' ':'spc','/':'slash','.':'dot','-':'minus','_':'shift-minus',':':'shift-semicolon','\n':'ret'}
        for c in s:self.key(names.get(c,('shift-'+c.lower()) if c.isupper() else c))
    def cmd(self,s):self.type(s+'\n');time.sleep(.25)
    def move(self,x,y):
        while self.mx!=x or self.my!=y:
            dx=max(-60,min(60,x-self.mx));dy=max(-60,min(60,y-self.my));self.mx+=dx;self.my+=dy
            ev=[]
            if dx:ev.append({'type':'rel','data':{'axis':'x','value':dx}})
            if dy:ev.append({'type':'rel','data':{'axis':'y','value':dy}})
            self.q('input-send-event',{'events':ev});time.sleep(.03)
        time.sleep(.08)
    def click(self,x,y,button='left'):
        self.move(x,y)
        for down in (True,False):
            self.q('input-send-event',{'events':[{'type':'btn','data':{'button':button,'down':down}}]});time.sleep(.08)
    def shot(self,name):
        time.sleep(.2);p=BUILD/(name+'.ppm');self.q('human-monitor-command',{'command-line':f'screendump {p}'})
        Image.open(p).save(BUILD/(name+'.png'));p.unlink()
    def stop(self):
        if self.proc.poll() is None:
            self.q('quit');self.proc.wait(timeout=5)
        self.f.close();self.s.close();self.sock.unlink(missing_ok=True)
def entries(disk):
    blob=disk.read_bytes()[:262144];assert blob[:8]==b'MAYFS001','MayFS signature missing'
    payload=blob[512:512+48*4160];h=2166136261
    for b in payload:h=((h^b)*16777619)&0xffffffff
    assert h==struct.unpack_from('<I',blob,8)[0],'MayFS checksum mismatch'
    result={}
    for i in range(48):
        at=512+i*4160;used,kind,parent,size=struct.unpack_from('<4I',blob,at)
        if used:
            name=blob[at+16:at+64].split(b'\0')[0].decode();data=blob[at+64:at+64+size].decode()
            result[name]=(i,kind,parent,data)
    return result

def main():
    disk=BUILD/'test-disk.img'
    with disk.open('wb') as f:f.truncate(8*1024*1024)
    vm=VM(disk,'test-first')
    try:
        vm.shot('desktop')
        vm.key('f1');vm.cmd('neofetch');vm.wait('Kernel: MayKernel x86 (custom)');vm.shot('terminal')
        vm.cmd('memtest');vm.wait('memtest: PASS')
        vm.cmd('mkdir Projects');vm.cmd('write Projects/persist.txt reboot-survives')
        vm.cmd('cat Projects/persist.txt');vm.wait('\nreboot-survives\n')
        vm.cmd('cd Projects');vm.cmd('pwd');vm.wait('\n/Projects\n')
        vm.cmd('mv persist.txt saved.txt');vm.wait('Renamed')
        vm.cmd('touch delete.txt');vm.cmd('rm delete.txt');vm.wait('Moved to Trash')
        # Use the actual emulated mouse, GUI modal, editor and Ctrl-S.
        vm.click(346,717);vm.shot('files')
        vm.click(370,212);vm.type('gui.txt\n');time.sleep(.3);vm.key('ret');time.sleep(.2)
        vm.type('GUI-PERSIST');vm.key('ctrl-s');time.sleep(.4);vm.shot('editor')
        data=entries(disk)
        assert data['gui.txt'][3]=='GUI-PERSIST',f'GUI editor did not save: {data.get("gui.txt")}'
        assert data['saved.txt'][3]=='reboot-survives'
        assert data['delete.txt'][2]==3,'rm did not move to Trash'
        print('PASS: boot, framebuffer, PS/2 keyboard + mouse, shell, allocator, GUI editor, ATA writes',flush=True)
    finally:vm.stop()
    vm=VM(disk,'test-reboot')
    try:
        vm.wait('MayOS: MayFS mounted');vm.key('f1');vm.cmd('cat Projects/saved.txt');vm.wait('\nreboot-survives\n')
        vm.cmd('cat Documents/gui.txt');vm.wait('\nGUI-PERSIST\n')
        print('PASS: cold reboot mounts MayFS and reads both shell- and GUI-created files',flush=True)
        vm.cmd('theme dark');vm.shot('dark-terminal')
    finally:vm.stop()
    vm=VM(None,'test-ram')
    try:
        vm.key('f1');vm.cmd('neofetch');vm.wait('Disk: RAM fallback')
        vm.cmd('write temp.txt ram-works');vm.cmd('cat temp.txt');vm.wait('\nram-works\n')
        print('PASS: diskless boot and RAM filesystem fallback',flush=True)
    finally:vm.stop()
    damaged=BUILD/'test-unknown.img'
    with damaged.open('wb') as f:f.write(b'UNKNOWN-DATA');f.truncate(8*1024*1024)
    before=damaged.read_bytes()
    vm=VM(damaged,'test-unknown')
    try:vm.wait('invalid/unknown disk; preserved, using RAM fallback')
    finally:vm.stop()
    assert damaged.read_bytes()==before,'Unknown disk was modified'
    print('PASS: unrecognized disk is preserved without writes',flush=True)
    print('All smoke tests passed.',flush=True)
if __name__=='__main__':main()

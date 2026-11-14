#!/usr/bin/env python3
"""Verify both hidden triggers, image rendering and modal input isolation."""
from smoke import VM,BUILD
from PIL import Image,ImageOps
from pathlib import Path
import time

def check_photo(path):
    actual=Image.open(path).convert('RGB')
    with Image.open(Path(__file__).resolve().parents[1]/'assets/easter-cat.jpg') as im:
        expected=ImageOps.exif_transpose(im).convert('RGB')
        expected.thumbnail((360,480),Image.Resampling.LANCZOS)
    for x,y in [(20,20),(130,50),(270,100),(130,210),(240,280),(80,390),(330,450)]:
        r,g,b=expected.getpixel((x,y));r>>=3;g>>=2;b>>=3
        rgb=((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))
        assert actual.getpixel(((1280-360)//2+x,124+y))==rgb,(x,y,actual.getpixel(((1280-360)//2+x,124+y)),rgb)

def main():
    disk=BUILD/'test-easter.img'
    with disk.open('wb') as f:f.truncate(8*1024*1024)
    vm=VM(disk,'test-easter')
    try:
        vm.move(50,50);vm.key('f1');vm.cmd('meow');vm.wait('MayOS: meow easter egg opened')
        vm.shot('easter-egg');check_photo(BUILD/'easter-egg.png')
        # Text is ignored while the modal is open; Enter/Esc returns to the shell.
        vm.type('xxx');vm.key('esc');vm.cmd('pwd');vm.wait('\n/\n')
        assert 'xxx' not in vm.log.read_text(),'Modal leaked keys into the terminal'
        vm.click(50,16) # Open the reference Settings / About page.
        for _ in range(4):vm.click(511,272)
        before=vm.log.read_text().count('MayOS: meow easter egg opened')
        assert before==1,'Easter egg opened before the fifth click'
        vm.click(511,272);time.sleep(.2)
        assert vm.log.read_text().count('MayOS: meow easter egg opened')==2
        vm.move(50,50);vm.shot('easter-about');check_photo(BUILD/'easter-about.png')
        vm.click(792,660) # Return button.
        vm.key('f1');vm.cmd('theme dark');vm.cmd('meow');vm.shot('easter-dark');check_photo(BUILD/'easter-dark.png')
        vm.click(842,93) # Title-bar close button.
        vm.cmd('write egg.txt still-works');vm.cmd('cat egg.txt');vm.wait('\nstill-works\n')
        print('PASS: meow command, five-click About trigger, exact photo pixels, keyboard isolation, Esc/button/title close, dark mode, shell recovery',flush=True)
    finally:vm.stop()
if __name__=='__main__':main()

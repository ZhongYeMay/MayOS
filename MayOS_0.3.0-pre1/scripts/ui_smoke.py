#!/usr/bin/env python3
"""Test the restored desktop through the running kernel's PS/2 input."""
from smoke import VM,BUILD,entries
from PIL import Image
import time

def wait_for(disk,check):
    until=time.monotonic()+5
    while time.monotonic()<until:
        try:
            data=entries(disk)
            if check(data):return data
        except (AssertionError,KeyError):pass
        time.sleep(.05)
    data=entries(disk);assert check(data),f'Filesystem condition timed out: {list(data)}'
    return data

def main():
    disk=BUILD/'test-ui.img'
    with disk.open('wb') as f:f.truncate(8*1024*1024)
    vm=VM(disk,'test-ui')
    try:
        # Screenshot contract: boot clean into user's macOS-like dark desktop.
        vm.shot('desktop-macos-md3-reference')
        image=Image.open(BUILD/'desktop-macos-md3-reference.png').convert('RGB')
        assert image.size==(1024,768),image.size
        assert image.getpixel((10,20))==(33,31,35),'macOS top menu color missing'
        assert image.getpixel((650,400))==(28,29,32),'old Material rail/window still obscures default desktop'
        assert image.getpixel((230,200))==(51,52,58),'reference descending wallpaper lines missing'
        assert image.getpixel((220,702))==(36,37,40),'centered dock background missing'
        assert image.getpixel((10,745))==(32,33,37),'obsolete full-width taskbar remains'
        vm.click(169,117);vm.click(169,117);vm.shot('notes-desktop-editor')
        vm.click(444,716);vm.click(295,236);vm.click(710,350);vm.shot('dark-settings')
        assert Image.open(BUILD/'dark-settings.png').getpixel((900,500))==(33,31,38)
        vm.click(440,330);vm.shot('light-settings')
        assert Image.open(BUILD/'light-settings.png').getpixel((900,500))==(255,247,255)
        vm.click(270,600);vm.shot('offline-help');vm.key('esc')
        vm.click(346,716);vm.shot('files')
        vm.click(450,300,'right');vm.shot('context-menu');vm.click(480,455)
        vm.click(700,500,'right');vm.click(730,577)
        data=wait_for(disk,lambda d:d['Welcome.txt-copy'][3]==d['Welcome.txt'][3])
        vm.click(450,382,'right');vm.click(480,503)
        for _ in 'Welcome.txt-copy':vm.key('backspace')
        vm.type('renamed.txt\n')
        wait_for(disk,lambda d:d['renamed.txt'][3]==data['Welcome.txt'][3])
        vm.key('delete');wait_for(disk,lambda d:d['renamed.txt'][2]==3)
        vm.click(210,340);vm.click(450,300,'right');vm.click(480,523)
        vm.click(210,260);vm.click(700,500,'right');vm.click(730,577)
        wait_for(disk,lambda d:d['renamed.txt'][2]==1)
        vm.click(490,212);vm.type('ProjectsUI\n')
        wait_for(disk,lambda d:d['ProjectsUI'][1:3]==(1,1))
        vm.move(500,120)
        vm.q('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':True}}]})
        time.sleep(.1);vm.move(620,180)
        vm.q('input-send-event',{'events':[{'type':'btn','data':{'button':'left','down':False}}]})
        time.sleep(.1);vm.shot('dragged-window')
        vm.click(949,180);vm.shot('maximized-files')
        vm.click(941,76);vm.click(916,180)
        vm.click(248,716);vm.type('terminal');vm.shot('start-menu');vm.key('ret')
        vm.cmd('help');vm.wait('F1 opens Terminal.')
        print('PASS: reference macOS top menu, centered Dock, Notes desktop, MD3 light/dark, file operations, windows, app search',flush=True)
    finally:vm.stop()
if __name__=='__main__':main()

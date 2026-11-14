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
        # Approved 0.5 design: near-black background, silver arcs, quiet Dock.
        vm.shot('desktop-macos-premium-050')
        image=Image.open(BUILD/'desktop-macos-premium-050.png').convert('RGB')
        assert image.size==(1024,768),image.size
        assert image.getpixel((10,20))==(5,5,6),'monochrome top menu color incorrect'
        assert max(image.getpixel((650,400))) < 25,'desktop unexpectedly obscured'
        assert max(image.getpixel((230,200))) < 25,'wallpaper is not ultra dark'
        assert image.getpixel((220,702))==(10,10,12),'floating dock surface incorrect'
        assert max(image.getpixel((10,745))) < 25,'obsolete full-width bar remains'
        # The reference-style status bar now exposes working native panels.
        vm.click(728,21);vm.shot('status-clock');vm.key('esc')
        vm.click(778,21);vm.shot('status-network');vm.key('esc')
        vm.click(828,21);vm.shot('status-appearance');vm.key('esc')
        vm.click(878,21);vm.shot('status-notifications');vm.key('esc')
        vm.click(928,21);vm.shot('status-power');vm.key('esc')
        vm.click(169,117);vm.click(169,117);vm.shot('notes-desktop-editor')
        vm.click(444,716);vm.shot('about-mayos-premium-050')
        about=Image.open(BUILD/'about-mayos-premium-050.png').convert('RGB')
        assert about.getpixel((390,390))==(48,48,51),'About outline is missing'
        assert about.getpixel((400,390))==(8,8,9),'About inner card is not near-black'
        vm.click(295,236);vm.click(710,350);vm.shot('dark-settings')
        assert Image.open(BUILD/'dark-settings.png').getpixel((900,500))==(8,8,9)
        vm.click(440,330);vm.shot('light-settings')
        assert Image.open(BUILD/'light-settings.png').getpixel((900,500))==(255,255,255)
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
        print('PASS: premium mono desktop, antialiased About and Dock, live statuses, settings light/dark, file ops, windows, app search',flush=True)
    finally:vm.stop()
if __name__=='__main__':main()

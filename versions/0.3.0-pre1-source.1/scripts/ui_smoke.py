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
        vm.click(295,236);vm.click(710,350);vm.shot('dark-settings')
        assert Image.open(BUILD/'dark-settings.png').getpixel((900,500))==(49,43,53)
        vm.click(440,330);vm.shot('light-settings')
        assert Image.open(BUILD/'light-settings.png').getpixel((900,500))==(255,255,255)
        vm.click(270,600);vm.shot('offline-help');vm.key('esc')
        vm.click(55,105);vm.shot('files')
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
        vm.click(941,76);vm.click(916,180);vm.click(140,740)
        vm.click(40,740);vm.type('terminal');vm.shot('start-menu');vm.key('ret')
        vm.cmd('help');vm.wait('F1 opens Terminal.')
        print('PASS: light/dark, file copy/rename/trash/restore, new folder, window controls, app search',flush=True)
    finally:vm.stop()
if __name__=='__main__':main()

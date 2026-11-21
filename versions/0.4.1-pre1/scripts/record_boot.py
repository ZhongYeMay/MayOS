#!/usr/bin/env python3
"""Record an actual QEMU boot and desktop interaction as a real-time MP4.

Frames come from QEMU's framebuffer via QMP. No simulated UI or reconstructed
boot frames are used. A private disk and copied OVMF variable store are used.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'


def sha(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


class Session:
    def __init__(self, args):
        self.args = args
        self.tag = f'record-{args.firmware}-{args.kernel}'
        self.log = BUILD / (self.tag + '.log')
        self.sock = BUILD / (self.tag + '.sock')
        self.frame = BUILD / (self.tag + '.ppm')
        self.sock.unlink(missing_ok=True)
        self.lock = threading.Lock()
        self.proc = self.connection = self.stream = None
        self.errors = []
        self.frames = 0
        self.stop_capture = threading.Event()
        self.first_frame = threading.Event()
        self.mx, self.my = 530, 390
        disk = BUILD / (self.tag + '-disk.img')
        with disk.open('wb') as handle:
            handle.truncate(8 * 1024 * 1024)
        command = [args.qemu, '-machine', 'pc', '-accel', 'tcg', '-cpu', 'max',
                   '-m', '512', '-vga', 'std', '-display', 'none', '-nic', 'none',
                   '-cdrom', str(args.iso), '-boot', 'd', '-no-reboot', '-S',
                   '-drive', f'file={disk},format=raw,if=ide,index=0',
                   '-serial', f'file:{self.log}',
                   '-qmp', f'unix:{self.sock},server=on,wait=off']
        if os.environ.get('QEMU_DATA_DIR'):
            command += ['-L', os.environ['QEMU_DATA_DIR']]
        if args.firmware == 'uefi':
            variable_store = BUILD / (self.tag + '-vars.fd')
            shutil.copyfile(args.ovmf_vars, variable_store)
            command += ['-drive', f'if=pflash,format=raw,readonly=on,file={args.ovmf_code}',
                        '-drive', f'if=pflash,format=raw,file={variable_store}']
        self.qemu_log = (BUILD / (self.tag + '-qemu.log')).open('w')
        self.proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=self.qemu_log)
        deadline = time.monotonic() + 20
        while not self.sock.exists():
            if self.proc.poll() is not None:
                raise RuntimeError('QEMU failed to start; inspect the recording QEMU log')
            if time.monotonic() > deadline:
                raise TimeoutError('QMP socket did not become available')
            time.sleep(.03)
        self.connection = socket.socket(socket.AF_UNIX)
        self.connection.settimeout(15)
        self.connection.connect(str(self.sock))
        self.stream = self.connection.makefile('rwb', buffering=0)
        json.loads(self.stream.readline())
        self.q('qmp_capabilities')

    def q(self, command, arguments=None):
        with self.lock:
            self.stream.write((json.dumps({'execute': command, 'arguments': arguments or {}})+'\n').encode())
            while True:
                line = self.stream.readline()
                if not line:
                    raise RuntimeError('QMP connection ended')
                result = json.loads(line)
                if 'error' in result:
                    raise RuntimeError(result['error'])
                if 'return' in result:
                    return result['return']

    def text(self):
        return self.log.read_text(errors='replace') if self.log.exists() else ''

    def wait(self, needle, timeout=90, offset=0):
        deadline = time.monotonic() + timeout
        while needle not in self.text()[offset:]:
            if self.proc.poll() is not None:
                raise RuntimeError(f'Guest exited before {needle!r}')
            if self.errors:
                raise RuntimeError(self.errors[0])
            if time.monotonic() > deadline:
                raise TimeoutError(f'Guest did not produce {needle!r}; inspect {self.log}')
            time.sleep(.05)

    def hold(self, seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            if self.errors:
                raise RuntimeError(self.errors[0])
            time.sleep(.05)

    def key(self, name):
        self.q('human-monitor-command', {'command-line': f'sendkey {name} 35'})
        self.hold(.09)

    def command(self, text):
        names = {' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus',
                 '_': 'shift-minus', ':': 'shift-semicolon', '\n': 'ret'}
        for character in text + '\n':
            self.key(names.get(character, ('shift-' + character.lower()) if character.isupper() else character))

    def click(self, x, y):
        while (self.mx, self.my) != (x, y):
            dx, dy = max(-50, min(50, x-self.mx)), max(-50, min(50, y-self.my))
            events = []
            if dx:
                events.append({'type':'rel','data':{'axis':'x','value':dx}})
            if dy:
                events.append({'type':'rel','data':{'axis':'y','value':dy}})
            self.q('input-send-event', {'events':events})
            self.mx += dx
            self.my += dy
            self.hold(.05)
        for down in (True, False):
            self.q('input-send-event', {'events':[{'type':'btn','data':{'button':'left','down':down}}]})
            self.hold(.1)

    def start_recording(self):
        self.args.output.parent.mkdir(parents=True, exist_ok=True)
        self.encode_log = self.args.output.with_suffix('.ffmpeg.log').open('w')
        self.encoder = subprocess.Popen([
            'ffmpeg', '-hide_banner', '-loglevel', 'warning', '-y',
            '-f', 'image2pipe', '-vcodec', 'ppm', '-framerate', str(self.args.fps), '-i', 'pipe:0',
            '-vf', 'scale=1024:768:force_original_aspect_ratio=decrease,pad=1024:768:(ow-iw)/2:(oh-ih)/2,setsar=1',
            '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '22', '-pix_fmt', 'yuv420p',
            '-movflags', '+faststart', '-an', str(self.args.output)
        ], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=self.encode_log)
        self.capture_started = time.monotonic()
        self.thread = threading.Thread(target=self.capture, daemon=True)
        self.thread.start()
        if not self.first_frame.wait(15):
            raise RuntimeError('No initial QEMU frame was captured')
        self.q('cont')

    def capture(self):
        try:
            while not self.stop_capture.is_set():
                next_time = self.capture_started + self.frames / self.args.fps
                delay = next_time - time.monotonic()
                if delay > 0 and self.stop_capture.wait(delay):
                    break
                self.q('screendump', {'filename': str(self.frame)})
                data = self.frame.read_bytes()
                if not data.startswith(b'P6\n'):
                    raise RuntimeError('QEMU did not return a PPM framebuffer')
                # Repeat the captured frame for any elapsed sampling intervals,
                # keeping the MP4 timeline aligned with actual elapsed time.
                due = max(self.frames+1, int((time.monotonic()-self.capture_started)*self.args.fps)+1)
                while self.frames < due:
                    self.encoder.stdin.write(data)
                    self.frames += 1
                self.first_frame.set()
        except Exception as error:
            if not self.stop_capture.is_set():
                self.errors.append(str(error))
        finally:
            self.first_frame.set()

    def finish_recording(self):
        self.stop_capture.set()
        self.thread.join(timeout=20)
        if self.thread.is_alive():
            raise RuntimeError('Frame capture did not stop')
        self.encoder.stdin.close()
        if self.encoder.wait(timeout=45) != 0:
            raise RuntimeError('FFmpeg encoding failed; inspect its log')
        self.encode_log.close()
        if self.errors:
            raise RuntimeError(self.errors[0])

    def close(self):
        if hasattr(self, 'thread') and self.thread.is_alive():
            self.stop_capture.set()
            self.thread.join(timeout=20)
        if hasattr(self, 'encoder') and self.encoder.poll() is None:
            self.encoder.stdin.close()
            try:
                self.encoder.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.encoder.kill()
        if self.proc and self.proc.poll() is None:
            try:
                self.q('quit')
            except Exception:
                self.proc.terminate()
            self.proc.wait(timeout=10)
        for stream in (self.stream, self.connection, self.qemu_log):
            if stream:
                stream.close()
        self.sock.unlink(missing_ok=True)
        self.frame.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', choices=('custom','linux'), required=True)
    parser.add_argument('--firmware', choices=('bios','uefi'), default='uefi')
    parser.add_argument('--iso', type=Path, default=BUILD/'mayos-dual.iso')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qemu', default=os.environ.get('QEMU64','qemu-system-x86_64'))
    parser.add_argument('--fps', type=int, default=10)
    parser.add_argument('--release', required=True, help='MayOS release version shown by this build')
    parser.add_argument('--source-commit', required=True, help='40-character Git commit used for the build')
    parser.add_argument('--ovmf-code', type=Path, default=os.environ.get('OVMF_CODE'))
    parser.add_argument('--ovmf-vars', type=Path, default=os.environ.get('OVMF_VARS'))
    args = parser.parse_args()
    args.iso, args.output = args.iso.resolve(), args.output.resolve()
    if not 1 <= args.fps <= 30:
        parser.error('--fps must be between 1 and 30')
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-pre[1-9][0-9]*)?', args.release):
        parser.error('--release must be a MayOS version')
    if not re.fullmatch(r'[0-9a-f]{40}', args.source_commit):
        parser.error('--source-commit must be a full Git commit SHA')
    if args.firmware=='uefi' and (not args.ovmf_code or not args.ovmf_vars):
        parser.error('UEFI recording requires --ovmf-code/--ovmf-vars or OVMF_CODE/OVMF_VARS')
    iso_hash = sha(args.iso)
    started = datetime.now(timezone.utc).isoformat()
    vm = Session(args)
    try:
        vm.start_recording()
        vm.wait('custom kernel', timeout=60)
        # Stop the menu countdown, show the real choices, then select a kernel.
        vm.key('down' if args.kernel=='linux' else 'up')
        vm.hold(3)
        vm.key('ret')
        vm.wait('MayOS: desktop ready', timeout=120)
        vm.hold(6)
        vm.click(280,336)
        vm.hold(5)
        vm.click(280,198)
        vm.hold(3)
        vm.key('f1')
        offset=len(vm.text())
        vm.command('neofetch')
        vm.wait('Kernel:', offset=offset)
        vm.hold(5)
        vm.command('write demo.txt UEFI-x64-ready')
        vm.command('cat demo.txt')
        vm.wait('\nUEFI-x64-ready\n')
        vm.hold(4)
        vm.click(863,170)
        vm.hold(4)
        vm.finish_recording()
    finally:
        vm.close()
    if sha(args.iso) != iso_hash:
        raise RuntimeError('ISO changed during capture; discard this recording and retry')
    result = {'iso':args.iso.name,'iso_sha256':iso_hash,'kernel':args.kernel,
              'mayos_release':args.release,'source_commit':args.source_commit,
              'firmware':args.firmware,'started_at_utc':started,'fps':args.fps,
              'frames':vm.frames,'duration_seconds':vm.frames/args.fps,
              'video':args.output.name,'video_sha256':sha(args.output),
              'capture':'Real-time QEMU framebuffer via QMP; no synthesized UI frames',
              'actions':['firmware_boot','grub_selection','desktop_about','open_source_credits',
                         'neofetch','gui_file_write_read'],
              'qemu':subprocess.check_output([args.qemu,'--version'],text=True).splitlines()[0]}
    if args.firmware=='uefi':
        result['ovmf_code_sha256']=sha(args.ovmf_code)
    args.output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()

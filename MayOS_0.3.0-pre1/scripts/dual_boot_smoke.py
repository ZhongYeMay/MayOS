#!/usr/bin/env python3
"""Boot both kernels through the same GRUB ISO and preserve a real MayFS disk.

This test intentionally uses -cdrom, never QEMU's -kernel shortcut. It uses a
private test disk, so the user's build/mayos-disk.img is never opened.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time

from PIL import Image
from smoke import BUILD, entries


class BootVM:
    def __init__(self, *, iso: Path, disk: Path, tag: str, qemu: str,
                 memory: int, timeout: float, ovmf_code: Path | None = None,
                 ovmf_vars: Path | None = None, cpu: str = 'max'):
        self.timeout = timeout
        self.serial_path = BUILD / f'{tag}-serial.sock'
        self.qmp_path = BUILD / f'{tag}-qmp.sock'
        self.log = BUILD / f'{tag}.log'
        self.stderr_path = BUILD / f'{tag}-qemu.log'
        for path in (self.serial_path, self.qmp_path):
            path.unlink(missing_ok=True)
        self.transcript = bytearray()
        self.mx, self.my = 530, 390
        self.serial = self.qmp = self.qmp_file = None
        self.proc = None
        self._log_file = self.log.open('wb')
        self._stderr_file = self.stderr_path.open('w')
        args = [qemu, '-machine', 'pc', '-accel', 'tcg', '-cpu', cpu,
                '-m', str(memory), '-vga', 'std', '-display', 'none',
                '-cdrom', str(iso), '-boot', 'd',
                '-drive', f'file={disk},format=raw,if=ide,index=0',
                '-serial', f'unix:{self.serial_path},server=on,wait=off',
                '-qmp', f'unix:{self.qmp_path},server=on,wait=off',
                '-nic', 'user,model=e1000', '-no-reboot', '-S']
        if (ovmf_code is None) != (ovmf_vars is None):
            raise ValueError('OVMF_CODE and an independent writable OVMF_VARS copy are both required')
        if ovmf_code is not None:
            if ovmf_code.resolve() == ovmf_vars.resolve():
                raise ValueError('OVMF_CODE and OVMF_VARS must be distinct files')
            args += ['-drive', f'if=pflash,format=raw,readonly=on,file={ovmf_code}',
                     '-drive', f'if=pflash,format=raw,file={ovmf_vars}']
        if os.environ.get('QEMU_DATA_DIR'):
            args += ['-L', os.environ['QEMU_DATA_DIR']]
        try:
            self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL,
                                         stderr=self._stderr_file)
            deadline = time.monotonic() + 20
            while not (self.serial_path.exists() and self.qmp_path.exists()):
                if self.proc.poll() is not None:
                    raise RuntimeError(f'QEMU failed: {self.stderr_path.read_text()}')
                if time.monotonic() >= deadline:
                    raise TimeoutError('QEMU control sockets did not appear')
                time.sleep(.03)
            self.serial = socket.socket(socket.AF_UNIX)
            self.serial.connect(str(self.serial_path))
            self.serial.setblocking(False)
            self.qmp = socket.socket(socket.AF_UNIX)
            self.qmp.settimeout(10)
            self.qmp.connect(str(self.qmp_path))
            self.qmp_file = self.qmp.makefile('rwb', buffering=0)
            greeting = json.loads(self.qmp_file.readline())
            if 'QMP' not in greeting:
                raise RuntimeError(f'Unexpected QMP greeting: {greeting}')
            self.q('qmp_capabilities')
            self.q('cont')
        except BaseException:
            self.stop()
            raise

    def q(self, command, arguments=None):
        self.qmp_file.write((json.dumps({'execute': command,
                                        'arguments': arguments or {}}) + '\n').encode())
        while True:
            line = self.qmp_file.readline()
            if not line:
                raise RuntimeError('QMP connection closed')
            response = json.loads(line)
            if 'error' in response:
                raise RuntimeError(response)
            if 'return' in response:
                return response['return']

    def pump(self):
        if self.serial is None:
            return
        while True:
            try:
                chunk = self.serial.recv(65536)
            except (BlockingIOError, ConnectionResetError):
                break
            if not chunk:
                break
            self.transcript.extend(chunk)
            self._log_file.write(chunk)
            self._log_file.flush()

    @property
    def text(self):
        self.pump()
        return self.transcript.decode('utf-8', errors='replace').replace('\r\n', '\n')

    def wait(self, needle, *, timeout=None, offset=0):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            text = self.text
            if needle in text[offset:]:
                return text
            if self.proc.poll() is not None:
                raise RuntimeError(f'Guest exited before {needle!r}; see {self.log}\n{text[-3000:]}')
            if time.monotonic() >= deadline:
                raise TimeoutError(f'Guest timed out waiting for {needle!r}; '
                                   f'see {self.log}\n{text[-3000:]}')
            time.sleep(.03)

    def key(self, key):
        self.q('human-monitor-command', {'command-line': f'sendkey {key} 25'})
        time.sleep(.07)
        self.pump()

    def keyboard_command(self, command):
        key_names = {' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus',
                     '_': 'shift-minus', ':': 'shift-semicolon', '\n': 'ret'}
        for character in command + '\n':
            self.key(key_names.get(character, ('shift-' + character.lower())
                                   if character.isupper() else character))

    def move(self, x, y):
        while (self.mx, self.my) != (x, y):
            dx = max(-60, min(60, x - self.mx))
            dy = max(-60, min(60, y - self.my))
            self.mx += dx
            self.my += dy
            events = []
            if dx:
                events.append({'type': 'rel', 'data': {'axis': 'x', 'value': dx}})
            if dy:
                events.append({'type': 'rel', 'data': {'axis': 'y', 'value': dy}})
            self.q('input-send-event', {'events': events})
            time.sleep(.03)
        time.sleep(.08)

    def click(self, x, y, button='left'):
        self.move(x, y)
        for down in (True, False):
            self.q('input-send-event', {'events': [
                {'type': 'btn', 'data': {'button': button, 'down': down}}
            ]})
            time.sleep(.08)

    def send_serial(self, text):
        self.serial.setblocking(True)
        try:
            self.serial.sendall(text.encode())
        finally:
            self.serial.setblocking(False)

    def shell_check(self, command, token):
        # Match an entire output line. The echoed printf command cannot pass this
        # assertion, even when echo is enabled by the serial terminal driver.
        offset = len(self.text)
        marker = f'MAYOS_TEST_{token}_STATUS'
        self.send_serial(command + f"; result=$?; printf '\\n{marker}=%s\\n' \"$result\"\n")
        pattern = re.compile(r'^' + re.escape(marker) + r'=(\d+)\r?$', re.MULTILINE)
        deadline = time.monotonic() + self.timeout
        while True:
            output = self.text[offset:]
            match = pattern.search(output)
            if match:
                if match.group(1) != '0':
                    raise AssertionError(f'Linux command failed ({match.group(1)}): '
                                         f'{command}\n{output}')
                return output
            if self.proc.poll() is not None or time.monotonic() >= deadline:
                raise AssertionError(f'No successful command result: {command}\n{output}')
            time.sleep(.03)

    def shot(self, name):
        # Give the guest event loop time to present the completed input update.
        time.sleep(.2)
        path = BUILD / f'{name}.ppm'
        # HMP paths here are generated under BUILD and have no user input.
        self.q('human-monitor-command', {'command-line': f'screendump {path}'})
        with Image.open(path) as screenshot:
            screenshot.save(BUILD / f'{name}.png')
        path.unlink()
        return BUILD / f'{name}.png'

    def menu_shot(self, name, timeout=4.5):
        # Serial menu labels can arrive before QEMU refreshes the VGA text
        # surface. SeaBIOS leaves the lower half black; GRUB draws a menu border
        # and keyboard instructions there. Wait for that draw instead of saving
        # a misleading firmware screenshot. Serial checks still verify labels.
        deadline = time.monotonic() + timeout
        while True:
            path = self.shot(name)
            with Image.open(path) as screenshot:
                width, height = screenshot.size
                lower_half = screenshot.convert('L').crop((0, height // 2, width, height))
                lit_pixels = sum(lower_half.histogram()[65:])
                if width <= 800 and height <= 600 and lit_pixels > 500:
                    # Complete the last text-cell redraw before saving the
                    # user-facing screenshot (the menu is already visible).
                    time.sleep(.4)
                    self.shot(name)
                    return
            if time.monotonic() >= deadline:
                raise AssertionError(f'GRUB menu was not visible on VGA; see {path}')
            self.pump()
            time.sleep(.15)

    def shutdown(self):
        self.send_serial('poweroff -f\n')
        deadline = time.monotonic() + 30
        while self.proc.poll() is None:
            self.pump()
            if time.monotonic() >= deadline:
                raise AssertionError('Linux did not power off QEMU within 30 seconds')
            time.sleep(.05)
        self.pump()
        assert self.proc.returncode == 0, f'QEMU exited with {self.proc.returncode}'
        assert 'Power down' in self.text, 'Missing Linux kernel power-off message'

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            try:
                if self.qmp_file is not None:
                    self.q('quit')
                else:
                    self.proc.terminate()
                self.proc.wait(timeout=5)
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                self.proc.kill()
                self.proc.wait(timeout=5)
        self.pump()
        for connection in (self.qmp_file, self.qmp, self.serial):
            if connection is not None:
                connection.close()
        self.qmp_file = self.qmp = self.serial = None
        self._log_file.close()
        self._stderr_file.close()
        for path in (self.serial_path, self.qmp_path):
            path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path,
                        default=Path(os.environ.get('MAYOS_DUAL_ISO', BUILD / 'mayos-dual.iso')))
    parser.add_argument('--qemu', default=os.environ.get('QEMU64', 'qemu-system-x86_64'))
    parser.add_argument('--memory', type=int, default=512)
    parser.add_argument('--timeout', type=float, default=90)
    args = parser.parse_args()
    iso = args.iso.resolve()
    if not iso.is_file():
        parser.error(f'dual-boot ISO not found: {iso}; run make dual first')
    manifest = json.loads((BUILD / 'linux/manifest.json').read_text())
    release = manifest['kernel_release']
    if re.fullmatch(r'[0-9A-Za-z._+\-]+', release) is None:
        parser.error('manifest contains an invalid kernel_release')
    disk = BUILD / 'test-dual-disk.img'
    with disk.open('wb') as handle:
        handle.truncate(8 * 1024 * 1024)
    options = dict(iso=iso, disk=disk, qemu=args.qemu, memory=args.memory,
                   timeout=args.timeout)

    vm = BootVM(tag='test-dual-mayos', **options)
    try:
        vm.wait('custom x86_64 kernel', timeout=20)
        vm.wait(f'Linux {release} - desktop', timeout=5)
        vm.menu_shot('test-dual-menu')
        # No keypress: exercise the configured default and its visible countdown.
        vm.wait('MayOS: desktop ready', timeout=30)
        vm.wait('MayOS: CPU long mode active (64-bit)')
        vm.shot('test-dual-desktop')
        vm.key('f1')
        vm.keyboard_command('neofetch')
        vm.wait('Kernel: MayKernel x86_64 (custom)')
        vm.keyboard_command('memtest')
        vm.wait('memtest: PASS')
        vm.keyboard_command('write dual.txt dual-boot-preserved')
        vm.keyboard_command('cat dual.txt')
        vm.wait('\ndual-boot-preserved\n')
        assert entries(disk)['dual.txt'][3] == 'dual-boot-preserved'
        print('PASS: same ISO boots the custom kernel by default; desktop and MayFS writes work', flush=True)
    finally:
        vm.stop()

    vm = BootVM(tag='test-dual-persistence', **options)
    try:
        vm.wait('MayOS: desktop ready', timeout=30)
        vm.wait('MayOS: MayFS mounted')
        vm.key('f1')
        vm.keyboard_command('cat dual.txt')
        vm.wait('\ndual-boot-preserved\n')
        print('PASS: custom-kernel cold boot reads the persisted test file', flush=True)
    finally:
        vm.stop()

    before = hashlib.sha256(disk.read_bytes()).hexdigest()
    vm = BootVM(tag='test-dual-linux', **options)
    try:
        vm.wait(f'Linux {release} - desktop', timeout=20)
        vm.key('down')
        vm.menu_shot('test-dual-linux-selected', timeout=10)
        vm.key('ret')
        vm.wait('MayOS Linux: ready')
        vm.wait('MayOS-Linux# ')
        vm.wait('MayOS: desktop ready')
        version_output = vm.shell_check(f'test "$(uname -r)" = "{release}" && uname -r', 'VERSION')
        assert re.search(r'^' + re.escape(release) + r'\r?$', version_output, re.MULTILINE), version_output
        vm.shell_check('test -r /proc/cpuinfo && test -d /sys/class && test -c /dev/console '
                       '&& test -c /dev/ttyS0 && grep -q " proc " /proc/mounts '
                       '&& grep -q " sysfs " /proc/mounts', 'MOUNTS')
        vm.shell_check('mkdir -p /tmp/mayos-test && printf linux-works > /tmp/mayos-test/file '
                       '&& test "$(cat /tmp/mayos-test/file)" = linux-works', 'FILES')
        vm.shell_check('! grep -E "^/dev/(sd|hd|vd)" /proc/mounts', 'NO_DISK_MOUNTS')
        vm.shell_check('test "$(cat /proc/1/comm)" = init', 'PID1_INIT')
        vm.shell_check('test -r /run/mayos-desktop.pid '
                       '&& test "$(cat /proc/$(cat /run/mayos-desktop.pid)/comm)" = mayos-desktop',
                       'GUI_PROCESS')
        # QEMU user-mode NAT supplies DHCP and DNS without outside Internet
        # access. Verify the actual guest lease, route, and resolver.
        vm.shell_check(
            'n=0; while ! ip -4 addr show | grep -q "10.0.2."; do '
            'n=$((n+1)); [ "$n" -lt 30 ] || exit 1; sleep 1; done; '
            'ip route | grep -q default && grep -q "^nameserver" /etc/resolv.conf',
            'NETWORK_DHCP_DNS')
        vm.shot('test-dual-linux-about')
        # PS/2 input targets the real Linux GUI, while the independent BusyBox
        # serial shell above probes the Linux rootfs. The GUI has its own RAM
        # file store; checking it through MayShell tests the actual interface.
        gui_offset = len(vm.text)
        vm.key('f1')
        vm.keyboard_command('neofetch')
        vm.wait(f'Kernel: Linux {release}', offset=gui_offset)
        vm.keyboard_command('write linux-demo.txt linux-gui-works')
        vm.keyboard_command('cat linux-demo.txt')
        vm.wait('\nlinux-gui-works\n', offset=gui_offset)
        # The RAM-only filesystem is verified by the Linux shell checks above
        # and the GUI write/read round-trip. Do not require an optional status
        # line that some GUI builds omit from their serial transcript.
        vm.click(55, 105)
        vm.click(370, 212)
        vm.keyboard_command('linux-editor.txt')
        time.sleep(.2)
        vm.key('ret')
        time.sleep(.2)
        vm.keyboard_command('GUI-LINUX-EDITOR')
        vm.key('ctrl-s')
        vm.shot('test-dual-linux-editor')
        editor_offset = len(vm.text)
        vm.key('f1')
        vm.keyboard_command('cat Documents/linux-editor.txt')
        vm.wait('\nGUI-LINUX-EDITOR\n', offset=editor_offset)
        vm.shot('test-dual-linux')
        prompt_offset = len(vm.text)
        vm.shell_check('printf %s "$$" > /tmp/mayos-test/serial-pid', 'SHELL_PID')
        vm.wait('MayOS-Linux# ', offset=prompt_offset)
        exit_offset = len(vm.text)
        vm.send_serial('exit\n')
        vm.wait('MayOS-Linux# ', offset=exit_offset, timeout=15)
        vm.shell_check('test "$$" != "$(cat /tmp/mayos-test/serial-pid)" '
                       '&& test -f /tmp/mayos-test/file', 'SHELL_RESPAWN')
        vm.shutdown()
        print(f'PASS: GRUB Linux selection boots {release}; native GUI/PS2, serial shell, proc/sys/dev, '
              'GUI and Linux RAM files, init respawn and shutdown work', flush=True)
    finally:
        vm.stop()
    after = hashlib.sha256(disk.read_bytes()).hexdigest()
    assert before == after, 'Linux changed the attached MayFS disk'
    assert entries(disk)['dual.txt'][3] == 'dual-boot-preserved'
    print('PASS: Linux leaves the complete attached MayFS disk byte-for-byte unchanged', flush=True)
    report = {'iso': iso.name, 'iso_sha256': hashlib.sha256(iso.read_bytes()).hexdigest(),
              'tested_at_utc': datetime.now(timezone.utc).isoformat(),
              'kernel_release': release,
              'qemu': subprocess.check_output([args.qemu, '--version'], text=True).splitlines()[0],
              'tests': ['grub_default_custom', 'custom_desktop', 'custom_allocator', 'custom_mayfs_write',
                        'custom_mayfs_cold_boot', 'grub_linux_selection', 'linux_uname',
                        'linux_mounts', 'linux_ram_files', 'linux_pid1_init',
                        'linux_gui_process', 'linux_gui_ps2_keyboard', 'linux_gui_ram_files',
                        'linux_gui_mouse', 'linux_gui_editor_ctrl_s',
                        'linux_shell_respawn', 'linux_clean_shutdown',
                        'mayfs_unchanged_by_linux'],
              'mayfs_sha256_before_linux': before, 'mayfs_sha256_after_linux': after}
    (BUILD / 'dual-boot-test-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('All dual-boot tests passed.', flush=True)


if __name__ == '__main__':
    main()

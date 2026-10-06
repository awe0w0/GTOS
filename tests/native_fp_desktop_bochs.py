#!/usr/bin/env python3
"""Strict FP users + desktop/game/input + AP workers in official Bochs.

RFB is loopback-only; raw frames are captured as PNG evidence. A new disposable
ATA image is exclusively created. Kernel-observed CPL3 input IRQ counters and
exact native cleanup are required, with no emulator state/fault waivers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import threading
import time
from PIL import Image
from native_fp_bochs import LOOPBACK_SHIM, read_exact

ROOT = Path(__file__).resolve().parents[1]


class Rfb:
    def __init__(self, port):
        self.stream = socket.create_connection(('127.0.0.1', port), timeout=3)
        version = read_exact(self.stream, 12)
        if not version.startswith(b'RFB 003.'):
            raise RuntimeError(f'unsupported RFB version {version!r}')
        self.stream.sendall(b'RFB 003.003\n')
        security, = struct.unpack('>I', read_exact(self.stream, 4))
        if security != 1:
            raise RuntimeError('expected no-auth loopback RFB')
        self.stream.sendall(b'\1')
        init = read_exact(self.stream, 24)
        width, height = struct.unpack('>HH', init[:4])
        self.pixel = struct.unpack('>BBBBHHHBBBxxx', init[4:20])
        self.pixel_bytes = self.pixel[0] // 8
        if self.pixel_bytes not in (1, 2, 4) or not self.pixel[3]:
            raise RuntimeError(f'unsupported RFB pixel format {self.pixel}')
        name_len, = struct.unpack('>I', init[20:24])
        read_exact(self.stream, name_len)
        self.stream.settimeout(None)
        self.lock = threading.Lock()
        self.image = Image.new('RGB', (width, height))
        self.updates = 0
        self.error = None
        # Bochs supports only its advertised native pixel format. Preserve it.
        self.stream.sendall(struct.pack('>BBHi', 2, 0, 1, 0))
        self.stream.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, width, height))
        threading.Thread(target=self.receive, daemon=True).start()

    def receive(self):
        try:
            while True:
                kind = read_exact(self.stream, 1)[0]
                if kind == 0:
                    count, = struct.unpack('>H', read_exact(self.stream, 3)[1:])
                    for _ in range(count):
                        x, y, width, height, encoding = struct.unpack('>HHHHi', read_exact(self.stream, 12))
                        if encoding == 0:
                            raw = read_exact(self.stream, width * height * self.pixel_bytes)
                            bits, depth, big, true, rm, gm, bm, rs, gs, bs = self.pixel
                            if self.pixel_bytes == 1:
                                rect = Image.frombytes('P', (width, height), raw)
                                palette = []
                                for pixel in range(256):
                                    palette.extend((((pixel >> rs) & rm) * 255 // rm,
                                                    ((pixel >> gs) & gm) * 255 // gm,
                                                    ((pixel >> bs) & bm) * 255 // bm))
                                rect.putpalette(palette); rect = rect.convert('RGB')
                            else:
                                converted = bytearray()
                                for index in range(0, len(raw), self.pixel_bytes):
                                    pixel = int.from_bytes(raw[index:index+self.pixel_bytes], 'big' if big else 'little')
                                    converted.extend((((pixel >> rs) & rm) * 255 // rm,
                                                      ((pixel >> gs) & gm) * 255 // gm,
                                                      ((pixel >> bs) & bm) * 255 // bm))
                                rect = Image.frombytes('RGB', (width, height), bytes(converted))
                            with self.lock:
                                if x + width > self.image.width or y + height > self.image.height:
                                    larger = Image.new('RGB', (max(self.image.width, x + width),
                                                                max(self.image.height, y + height)))
                                    larger.paste(self.image, (0, 0)); self.image = larger
                                self.image.paste(rect, (x, y)); self.updates += 1
                        elif encoding == -223:  # desktop resize
                            with self.lock:
                                self.image = Image.new('RGB', (width, height))
                        else:
                            raise RuntimeError(f'unrequested RFB encoding {encoding}')
                    self.stream.sendall(struct.pack('>BBHHHH', 3, 1, 0, 0,
                                                    self.image.width, self.image.height))
                elif kind == 2:
                    continue  # bell
                elif kind == 3:
                    size, = struct.unpack('>I', read_exact(self.stream, 7)[3:])
                    read_exact(self.stream, size)
                else:
                    raise RuntimeError(f'unexpected RFB message {kind}')
        except (OSError, RuntimeError, ValueError) as error:
            self.error = error

    def key(self, symbol, hold=.1):
        code = {'ret': 0xff0d, 'esc': 0xff1b, 'right': 0xff53, 'left': 0xff51}.get(symbol)
        if code is None:
            code = ord(symbol)
        self.stream.sendall(struct.pack('>BBHI', 4, 1, 0, code))
        time.sleep(hold)
        self.stream.sendall(struct.pack('>BBHI', 4, 0, 0, code))
        time.sleep(.12)

    def mouse(self, x, y):
        self.stream.sendall(struct.pack('>BBHH', 5, 0, x, y))

    def screenshot(self, path):
        with self.lock:
            image = self.image.copy()
        if self.error or not self.updates or len(image.getcolors(image.width * image.height) or []) < 4:
            image.save(path.with_name(path.stem + '-incomplete.png'))
            raise RuntimeError(f'no reliable nonblank RFB screenshot: {self.error}, updates={self.updates}, pixel={self.pixel}')
        image.save(path)
        return image


def require(value, message):
    if not value:
        raise RuntimeError(message)
    print('PASS: ' + message, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--iso', default=str(ROOT / 'GTOS-native-fp-test.iso'))
    parser.add_argument('--cpu', default='corei7_sandy_bridge_2600k')
    parser.add_argument('--cpus', type=int, default=4)
    parser.add_argument('--memory', type=int, default=64)
    parser.add_argument('--ips', type=int, default=300000000)
    parser.add_argument('--bios', help='unmodified firmware ROM, recorded with SHA256')
    parser.add_argument('--vga-rom', help='unmodified VGA ROM, recorded with SHA256')
    args = parser.parse_args()
    root, out, iso = Path(args.root).resolve(), Path(args.output).resolve(), Path(args.iso).resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'apps.img'
    if disk.exists():
        raise RuntimeError('choose a fresh output directory; never overwrite a disk')
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk)], check=True)
    shim_source, shim_binary = out / 'loopback_bind.c', out / 'loopback_bind.so'
    shim_source.write_text(LOOPBACK_SHIM)
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror', str(shim_source),
                    '-o', str(shim_binary), '-ldl'], check=True)
    env = os.environ.copy()
    env.update(BXSHARE=str(root / 'usr/share/bochs'),
               LTDL_LIBRARY_PATH=str(root / 'usr/lib/x86_64-linux-gnu/bochs/plugins'),
               LD_PRELOAD=str(shim_binary), LC_ALL='C')
    bochs = root / 'usr/bin/bochs-bin'
    version = subprocess.run([str(bochs), '--help'], env=env, capture_output=True, text=True)
    (out / 'bochs-version.txt').write_text(version.stdout + version.stderr)
    (out / 'iso.sha256').write_text(hashlib.sha256(iso.read_bytes()).hexdigest() + '  ' + str(iso) + '\n')
    bios = Path(args.bios).resolve() if args.bios else root / 'usr/share/bochs/BIOS-bochs-latest'
    vgarom = Path(args.vga_rom).resolve() if args.vga_rom else root / 'usr/share/vgabios/vgabios.bin'
    (out / 'firmware.sha256').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path) + '\n' for path in (bios, vgarom)))
    bochs_log, guest_log = out / 'bochs.log', out / 'guest.log'
    config = '\n'.join([
        'config_interface: textconfig', 'display_library: rfb, options="timeout=30,hideIPS"',
        f'romimage: file="{bios}"',
        f'vgaromimage: file="{vgarom}"',
        f'megs: {args.memory}',
        f'cpu: model={args.cpu}, count={args.cpus}, ips={args.ips}, reset_on_triple_fault=0',
        'clock: sync=none, time0=946684800', 'vga: extension=vbe, update_freq=20, realtime=0',
        'pci: enabled=1, chipset=i440fx, slot1=pcivga',
        'ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14',
        f'ata0-master: type=disk, path="{disk}", mode=flat, cylinders=0, heads=16, spt=63',
        f'ata0-slave: type=cdrom, path="{iso}", status=inserted',
        'boot: cdrom', 'mouse: enabled=1, type=ps2', 'keyboard: type=mf',
        'port_e9_hack: enabled=1', f'log: {bochs_log}',
        'panic: action=fatal', 'error: action=report', 'info: action=report', 'debug: action=ignore',
        'sound: driver=dummy', 'speaker: enabled=0', 'com1: enabled=0', 'parport1: enabled=0', ''])
    config_path = out / 'bochsrc'; config_path.write_text(config)
    command = [str(bochs), '-q', '-f', str(config_path)]
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    rfb = None
    with guest_log.open('w') as output, (out / 'bochs-stderr.log').open('w') as errors:
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL, stdout=output, stderr=errors)
        def text():
            return guest_log.read_text(errors='replace')
        def wait(marker, timeout=180):
            nonlocal rfb
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                log = bochs_log.read_text(errors='replace') if bochs_log.exists() else ''
                if rfb is None:
                    match = re.search(r'listening for connections on port (\d+)', log)
                    if match:
                        rfb = Rfb(int(match[1]))
                        (out / 'rfb-pixel-format.json').write_text(json.dumps(rfb.pixel) + '\n')
                if rfb and rfb.error:
                    raise RuntimeError(f'RFB capture failed: {rfb.error}')
                seen = text()
                if marker in seen:
                    return
                if process.poll() is not None or 'PANIC ' in seen or 'NATIVE RUNTIME FAIL' in seen:
                    raise RuntimeError(f'guest failed before {marker}:\n{seen[-6000:]}')
                time.sleep(.03)
            raise RuntimeError(f'guest deadline waiting for {marker}:\n{text()[-6000:]}')
        try:
            wait('DESKTOP READY')
            require('NATIVE FP DESKTOP TWO USERS READY' in text(), 'two independently built FP ELF users admitted')
            require('NATIVE RUNTIME PASS' not in text(), 'native users still live when desktop input begins')
            for index in range(12):
                rfb.key('2', .02); rfb.mouse(200 + index % 3, 200 + index % 2)
            wait('UI HARDWARE')
            rfb.key('3'); rfb.key('i'); wait('APP INSTALL OK')
            rfb.key('ret'); wait('APP LAUNCH OK')
            time.sleep(.5)
            before = rfb.screenshot(out / 'concurrent-fp-game-before.png')
            rfb.key('right', .4)
            time.sleep(.3)
            after = rfb.screenshot(out / 'concurrent-fp-game-after.png')
            require('DESKTOP MODE LEGACY' in text(), 'classic RFB test records the legacy desktop renderer')
            # Classic RFB presents the 320x200 VGA guest at 2x scale under a
            # 30-pixel toolbar. Only the teal paddle occupies this lower band.
            def paddle_center(image):
                points = [x for y in range(310, 345) for x in range(40, 600)
                          if image.getpixel((x, y)) == (36, 255, 170)]
                return sum(points) / len(points) if points else None
            before_x, after_x = paddle_center(before), paddle_center(after)
            require(before_x is not None and after_x is not None and after_x > before_x + 20,
                    'screenshot-derived Catch paddle moves right during live FP execution')
            (out / 'paddle-positions.json').write_text(json.dumps({'before': before_x, 'after': after_x}) + '\n')
            require('NATIVE FP CPL3 FAULT STATE VERIFIED' not in text(), 'game input overlaps both live FP users')
            wait('NATIVE FP DESKTOP ISOLATION PASS')
            wait('NATIVE RUNTIME PASS')
            require('BROWSER PLATFORM PROBE PASS ABI1' in text() and 'BROWSER PROBE EXIT 00000000' in text(),
                    'original independent ABI1 browser fixture still passes')
            wait('SCHEDULER RUNTIME PASS')
            require(f'WORKER READY {args.cpus - 1:08X}' in text() and 'WORKER FAILED 00000000' in text()
                    and 'CPU ONLINE 00000001' in text(),
                    'AP workers ready while general scheduler remains BSP-only' if args.cpus > 1
                    else 'UP firmware reports one BSP and no AP workers')
            if args.cpus > 1:
                wait('WORKER PERIODIC PASS')
                ids = [line.rsplit(' ', 1)[1] for line in text().splitlines()
                       if line.startswith('WORKER JOB VERIFIED APIC ')]
                require(len(ids) == args.cpus - 1 and len(set(ids)) == args.cpus - 1,
                        'each distinct AP completes repeated verified integer work')
            if args.cpus > 1:
                require('WORKER RUNTIME FAIL' not in text(), 'AP work remains healthy alongside FP users and GUI')
            counts = {}
            for kind in ('KEYBOARD', 'MOUSE'):
                match = re.search(rf'NATIVE FP CPL3 {kind} IRQS ([0-9A-F]+)', text())
                require(match and int(match[1], 16) > 0, f'kernel verified actual CPL3 {kind.lower()} IRQ entry')
                counts[kind] = int(match[1], 16)
            (out / 'interrupt-summary.json').write_text(json.dumps(counts, indent=2) + '\n')
            rfb.key('r'); wait('APP RESTART OK'); rfb.key('esc'); wait('APP CLOSE OK')
            rfb.screenshot(out / 'desktop-after-fp-reap.png')
            coverage = 'FP-native/desktop/AP' if args.cpus > 1 else 'FP-native/desktop (UP; no AP-work claim)'
            print('Strict Bochs ' + coverage + ' acceptance passed. Evidence: ' + str(out), flush=True)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            if rfb:
                rfb.stream.close()


if __name__ == '__main__':
    main()

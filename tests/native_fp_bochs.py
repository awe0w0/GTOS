#!/usr/bin/env python3
"""Run strict GRUB/i386 native-FP guests in Bochs with real PS/2 IRQ input.

Bochs's RFB plugin has no bind-address option. A tiny process-local shim limits
its listener to 127.0.0.1; it does not change emulator CPU/device behavior or any
host network setting. No external VNC viewer, desktop, or system install is used.
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


LOOPBACK_SHIM = r'''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
int bind(int fd, const struct sockaddr *addr, socklen_t size) {
    int (*original)(int, const struct sockaddr *, socklen_t) = dlsym(RTLD_NEXT, "bind");
    if (!original) { errno = ENOSYS; return -1; }
    if (addr && addr->sa_family == AF_INET && size >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in local = *(const struct sockaddr_in *)addr;
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return original(fd, (const struct sockaddr *)&local, sizeof(local));
    }
    if (addr && addr->sa_family == AF_INET6) { errno = EAFNOSUPPORT; return -1; }
    return original(fd, addr, size);
}
'''


def read_exact(stream, length):
    result = b''
    while len(result) < length:
        data = stream.recv(length - len(result))
        if not data:
            raise RuntimeError('RFB connection closed')
        result += data
    return result


def connect_rfb(port):
    stream = socket.create_connection(('127.0.0.1', port), timeout=2)
    version = read_exact(stream, 12)
    # Bochs 3.0 implements the original RFB 3.3 no-auth local protocol.
    if not version.startswith(b'RFB 003.'):
        raise RuntimeError(f'unrecognized RFB protocol {version!r}')
    stream.sendall(b'RFB 003.003\n')
    security, = struct.unpack('>I', read_exact(stream, 4))
    if security != 1:
        raise RuntimeError(f'unsupported local RFB security type {security}')
    stream.sendall(b'\1')
    init = read_exact(stream, 24)
    width, height = struct.unpack('>HH', init[:4])
    name_size, = struct.unpack('>I', init[20:24])
    name = read_exact(stream, name_size).decode(errors='replace')
    # Bochs sends framebuffer updates proactively; drain them so input-only
    # test execution cannot block behind a full RFB output socket.
    stream.settimeout(None)
    def drain():
        try:
            while stream.recv(65536):
                pass
        except OSError:
            pass
    threading.Thread(target=drain, daemon=True).start()
    return stream, {'server_version': version.decode().strip(), 'name': name,
                    'initial_width': width, 'initial_height': height, 'port': port}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, help='unpacked official Bochs package root')
    parser.add_argument('--iso', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--cpu', default='corei7_sandy_bridge_2600k')
    parser.add_argument('--exclude-features', default='')
    parser.add_argument('--memory', type=int, default=64, help='guest MiB')
    parser.add_argument('--cpus', type=int, default=1)
    parser.add_argument('--ips', type=int, help='total virtual IPS (default: 300M per configured CPU)')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--panic', action='store_true')
    args = parser.parse_args()
    # Bochs charges SMP scheduling quanta for parked APs too; scale the clock
    # so the unchanged 120M-iteration BSP payload fits its 1500-tick bound.
    if args.ips is None:
        args.ips = 300000000 * args.cpus
    root, iso, out = Path(args.root).resolve(), Path(args.iso).resolve(), Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    bochs = root / 'usr/bin/bochs-bin'
    plugins = root / 'usr/lib/x86_64-linux-gnu/bochs/plugins'
    if not bochs.is_file() or not iso.is_file():
        parser.error('unpacked bochs-bin and input ISO must exist')
    env = os.environ.copy()
    env['BXSHARE'] = str(root / 'usr/share/bochs')
    env['LTDL_LIBRARY_PATH'] = str(plugins)
    env['LC_ALL'] = 'C'
    shim_c, shim_so = out / 'loopback_bind.c', out / 'loopback_bind.so'
    shim_c.write_text(LOOPBACK_SHIM)
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(shim_c), '-o', str(shim_so), '-ldl'], check=True)
    env['LD_PRELOAD'] = str(shim_so)
    (out / 'environment.json').write_text(json.dumps({key: env[key] for key in
        ('BXSHARE', 'LTDL_LIBRARY_PATH', 'LC_ALL', 'LD_PRELOAD')}, indent=2) + '\n')
    version = subprocess.run([str(bochs), '--help'], env=env, capture_output=True, text=True)
    (out / 'bochs-version.txt').write_text(version.stdout + version.stderr)
    (out / 'iso.sha256').write_text(hashlib.sha256(iso.read_bytes()).hexdigest() + '  ' + str(iso) + '\n')
    bochs_log, guest_log = out / 'bochs.log', out / 'guest.log'
    # A repeated run must not connect using the previous run's RFB port log.
    bochs_log.write_text('')
    excludes = f', exclude_features="{args.exclude_features}"' if args.exclude_features else ''
    config = '\n'.join([
        'config_interface: textconfig',
        'display_library: rfb, options="timeout=30,hideIPS"',
        f'romimage: file="{root}/usr/share/bochs/BIOS-bochs-latest"',
        f'vgaromimage: file="{root}/usr/share/vgabios/vgabios.bin"',
        f'megs: {args.memory}',
        f'cpu: model={args.cpu}, count={args.cpus}, ips={args.ips}, reset_on_triple_fault=0{excludes}',
        'clock: sync=none, time0=946684800',
        'vga: update_freq=10, realtime=0',
        'ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14',
        f'ata0-master: type=cdrom, path="{iso}", status=inserted',
        'boot: cdrom',
        'mouse: enabled=1, type=ps2',
        'keyboard: type=mf',
        'port_e9_hack: enabled=1',
        f'log: {bochs_log}',
        'panic: action=fatal', 'error: action=report', 'info: action=report', 'debug: action=ignore',
        'sound: driver=dummy', 'speaker: enabled=0', 'com1: enabled=0', 'parport1: enabled=0',
        ''])
    config_path = out / 'bochsrc'
    config_path.write_text(config)
    command = [str(bochs), '-q', '-f', str(config_path)]
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    injected, stream, result = 0, None, 1
    with guest_log.open('w') as output, (out / 'bochs-stderr.log').open('w') as errors:
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL, stdout=output, stderr=errors)
        deadline = time.monotonic() + args.timeout
        try:
            while process.poll() is None:
                text = guest_log.read_text(errors='replace')
                log = bochs_log.read_text(errors='replace') if bochs_log.exists() else ''
                if stream is None:
                    match = re.search(r'listening for connections on port (\d+)', log)
                    if match:
                        stream, details = connect_rfb(int(match[1]))
                        (out / 'rfb.json').write_text(json.dumps(details, indent=2) + '\n')
                if 'NATIVE FP SMOKE FAIL' in text or 'NATIVE FP DIAGNOSTIC PASS' in text:
                    raise RuntimeError('strict guest failed or used an emulator waiver')
                if args.panic:
                    if 'NATIVE FP EXPECT CPL0 NM' in text and 'PANIC EXCEPTION vector=00000007' in text:
                        if 'NATIVE FP SMOKE PASS' in text:
                            raise RuntimeError('fatal kernel #NM reported ordinary completion')
                        result = 0
                        break
                elif 'NATIVE FP SMOKE PASS\n' in text:
                    result = 0
                    break
                if stream and 'NATIVE FP IRQ WINDOW' in text and not args.panic:
                    stream.sendall(struct.pack('>BBHI', 4, 1 if injected % 2 == 0 else 0, 0, ord('a')))
                    stream.sendall(struct.pack('>BBHH', 5, 0, 100 + (injected % 2) * 3, 100 + injected % 3))
                    injected += 1
                if time.monotonic() >= deadline:
                    raise RuntimeError('Bochs guest deadline expired')
                time.sleep(.015)
            if result:
                raise RuntimeError(f'Bochs exited without a strict guest completion (exit {process.poll()})')
        except (RuntimeError, OSError, ValueError) as error:
            print(guest_log.read_text(errors='replace'), file=sys.stderr)
            print((out / 'bochs-stderr.log').read_text(errors='replace')[-4000:], file=sys.stderr)
            print(f'FAIL: {error}', file=sys.stderr)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if stream:
                stream.close()
            (out / 'input-events.txt').write_text(f'RFB keyboard and pointer batches: {injected}\n')
    if result == 0:
        print('PASS strict Bochs guest' if not args.panic else 'PASS expected fatal CPL0 #NM')
    return result


if __name__ == '__main__':
    sys.exit(main())

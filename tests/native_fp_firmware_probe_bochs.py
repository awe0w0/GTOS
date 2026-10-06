#!/usr/bin/env python3
"""Observe a pre-paging BSP, using an unmodified selected firmware image."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from native_fp_bochs import LOOPBACK_SHIM, connect_rfb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True)
    parser.add_argument('--bios', required=True)
    parser.add_argument('--iso', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--cpu', default='corei7_sandy_bridge_2600k')
    parser.add_argument('--cpus', type=int, default=1)
    parser.add_argument('--ips', type=int, default=300000000)
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    root, bios, iso, out = (Path(p).resolve() for p in
                            (args.root, args.bios, args.iso, args.output))
    out.mkdir(parents=True, exist_ok=True)
    bochs = root / 'usr/bin/bochs-bin'
    vgabios = root / 'usr/share/vgabios/vgabios.bin'
    env = os.environ.copy()
    env.update(BXSHARE=str(root / 'usr/share/bochs'),
               LTDL_LIBRARY_PATH=str(root / 'usr/lib/x86_64-linux-gnu/bochs/plugins'),
               LC_ALL='C')
    shim_c, shim_so = out / 'loopback_bind.c', out / 'loopback_bind.so'
    shim_c.write_text(LOOPBACK_SHIM)
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(shim_c), '-o', str(shim_so), '-ldl'], check=True)
    env['LD_PRELOAD'] = str(shim_so)
    (out / 'environment.json').write_text(json.dumps({key: env[key] for key in
        ('BXSHARE', 'LTDL_LIBRARY_PATH', 'LC_ALL', 'LD_PRELOAD')}, indent=2) + '\n')
    version = subprocess.run([str(bochs), '--help'], env=env, capture_output=True, text=True)
    (out / 'bochs-version.txt').write_text(version.stdout + version.stderr)
    (out / 'inputs.sha256').write_text(''.join(
        hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path) + '\n'
        for path in (bochs, bios, vgabios, iso)))
    config_path, bochs_log, guest_log = out / 'bochsrc', out / 'bochs.log', out / 'guest.log'
    bochs_log.write_text('')
    config_path.write_text('\n'.join([
        'config_interface: textconfig',
        'display_library: rfb, options="timeout=30,hideIPS"',
        f'romimage: file="{bios}"', f'vgaromimage: file="{vgabios}"',
        'megs: 64',
        f'cpu: model={args.cpu}, count={args.cpus}, ips={args.ips}, reset_on_triple_fault=0',
        'clock: sync=none, time0=946684800', 'vga: update_freq=10, realtime=0',
        'ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14',
        f'ata0-master: type=cdrom, path="{iso}", status=inserted', 'boot: cdrom',
        'mouse: enabled=1, type=ps2', 'keyboard: type=mf', 'port_e9_hack: enabled=1',
        f'log: {bochs_log}', 'panic: action=fatal', 'error: action=report',
        'info: action=report', 'debug: action=ignore', 'sound: driver=dummy',
        'speaker: enabled=0', 'com1: enabled=0', 'parport1: enabled=0', '']))
    command = [str(bochs), '-q', '-f', str(config_path)]
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    stream, result = None, {'complete': False, 'cpu': args.cpu, 'cpus': args.cpus, 'ips': args.ips,
                             'bios': str(bios), 'read_only_control_state': True}
    with guest_log.open('w') as output, (out / 'bochs-stderr.log').open('w') as errors:
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=errors)
        deadline = time.monotonic() + args.timeout
        try:
            while process.poll() is None:
                text = guest_log.read_text(errors='replace')
                log = bochs_log.read_text(errors='replace')
                if stream is None:
                    match = re.search(r'listening for connections on port (\d+)', log)
                    if match:
                        stream, details = connect_rfb(int(match[1]))
                        (out / 'rfb.json').write_text(json.dumps(details, indent=2) + '\n')
                if 'NATIVE FP FIRMWARE PROBE FAIL' in text:
                    raise RuntimeError('guest probe preconditions failed')
                if 'NATIVE FP FIRMWARE PROBE COMPLETE\n' in text:
                    result['complete'] = True
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError('guest firmware probe deadline expired')
                time.sleep(.02)
            if not result['complete']:
                raise RuntimeError(f'Bochs exited without completion ({process.poll()})')
        except (RuntimeError, OSError, ValueError) as error:
            result['error'] = str(error)
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
            result['process_returncode'] = process.returncode
            (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(guest_log.read_text(errors='replace'))
    print(json.dumps(result))
    return 0 if result['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

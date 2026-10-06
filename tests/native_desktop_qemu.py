#!/usr/bin/env python3
"""Actual desktop input/IRQ regression while independently loaded CPL3 ELF runs.
Uses a dedicated test ISO with a five-second peer and a new private disk. The
normal ISO is unchanged. Interrupt traces establish the IRQ origin privilege,
not just that keyboard events were queued before a later screenshot.
"""
import argparse
import gzip
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from PIL import Image
import qemu_smoke
from qemu_smoke import Guest, check

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default=str(ROOT / 'obj/native-desktop-qemu'))
    args = parser.parse_args()
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'apps.img'
    if disk.exists():
        raise RuntimeError('Choose a new output directory; never overwrite a disk')
    qemu_smoke.BOOT_ISO = ROOT / 'GTOS-native-test.iso'
    if not qemu_smoke.BOOT_ISO.exists():
        raise RuntimeError('Build make GTOS-native-test.iso first')
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk)], check=True)
    g = Guest(out, disk, 64, 4, wait_ready=False, trace_interrupts=True)
    try:
        g.wait('DESKTOP READY', 15)
        check('NATIVE ELF PROCESSES READY' in g.text() and 'NATIVE RUNTIME PASS' not in g.text(),
              'external native programs are still running when input begins')
        for _ in range(15):
            g.call('human-monitor-command', {'command-line': 'sendkey 2 10'})
            g.call('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': 1}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': 1}}]})
            time.sleep(.035)
        g.wait('UI HARDWARE')
        g.key('3'); g.key('i'); g.wait('APP INSTALL OK')
        g.key('ret'); g.wait('APP LAUNCH OK'); g.key('right', 200)
        check('NATIVE RUNTIME PASS' not in g.text(), 'installed game accepts input while peer is live')
        path = out / 'concurrent-native-game.ppm'
        g.call('screendump', {'filename': str(path)})
        Image.open(path).save(out / 'concurrent-native-game.png')
        g.wait('NATIVE RUNTIME PASS', 15)
        text = g.text()
        check(text.index('APP LAUNCH OK') < text.index('NATIVE CPL3 PEER SURVIVED'),
              'game launch precedes native peer exit in the guest log')
        check('BROWSER PLATFORM PROBE PASS ABI1' in text and 'BROWSER PROBE EXIT 00000000' in text,
              'independent browser-platform fixture passes and exits normally')
        g.wait('SCHEDULER RUNTIME PASS')
        g.verify_workers(4, periodic=True)
        g.key('r'); g.wait('APP RESTART OK'); g.key('esc'); g.wait('APP CLOSE OK')
    finally:
        g.close()
    trace = out / 'interrupt-trace.log'
    counts = {'21': 0, '2c': 0}
    expression = re.compile(r'\bv=(21|2c)\s+.*\bi=0\s+cpl=3\b', re.IGNORECASE)
    with trace.open(errors='replace') as file:
        for line in file:
            match = expression.search(line)
            if match:
                counts[match.group(1).lower()] += 1
    for vector, count in counts.items():
        check(count > 0, f'hardware IRQ 0x{vector} actually interrupted CPL3 ({count} events)')
    (out / 'interrupt-summary.txt').write_text('\n'.join(
        f'CPL3 hardware IRQ 0x{vector}: {count}' for vector, count in counts.items()) + '\n')
    # Preserve the complete trace without retaining its highly repetitive raw size.
    with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
        shutil.copyfileobj(source, target)
    trace.unlink()
    print('Concurrent native/desktop/AP acceptance passed. Evidence: ' + str(out), flush=True)

if __name__ == '__main__':
    main()

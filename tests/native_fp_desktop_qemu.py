#!/usr/bin/env python3
"""Actual modern-desktop/game/AP work while TWO FP CPL3 ELF users run.
Default mode retains strict pointer comparisons. --diagnostic uses a separate
ELF/ISO, qualifies QEMU's precise pointer omission with an IF-clear kernel
microprobe, and MUST NOT be described as strict FP ownership acceptance.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from PIL import Image
import qemu_smoke
from qemu_smoke import Guest, check
from desktop_qemu import paddle as desktop_paddle

ROOT = Path(__file__).resolve().parents[1]
STRICT_MARKER = 'NATIVE FP DESKTOP ISOLATION PASS'
DIAGNOSTIC_MARKER = 'NATIVE FP DESKTOP DIAGNOSTIC PASS (POINTER GAPS REMAIN)'
QUALIFIER = 'NATIVE FP DIRECT IF-CLEAR POINTER OMISSION QUALIFIED'


def live(text):
    return ('NATIVE FP CPL3 FAULT LIVE' in text and 'NATIVE FP CPL3 PEER LIVE' in text
            and 'NATIVE FP CPL3 FAULT STATE VERIFIED' not in text
            and 'NATIVE CPL3 PEER SURVIVED' not in text and 'NATIVE RUNTIME PASS' not in text)


def screenshot(guest, out, name):
    path = out / (name + '.ppm')
    guest.call('screendump', {'filename': str(path)})
    im = Image.open(path).convert('RGB')
    im.save(out / (name + '.png'))
    check(im.size == (800, 600), 'native 800x600 framebuffer screenshot')
    return im


def paddle(image):
    # Catch renders its 32x5 paddle in palette index 8 (0xEEDF83).
    # No text or other game element uses this exact color.
    points = [(x, y) for y in range(image.height) for x in range(image.width)
              if image.getpixel((x, y)) == (0xEE, 0xDF, 0x83)]
    check(len(points) >= 160, 'visible Catch paddle pixels')
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    check((max(xs) - min(xs) + 1) * (max(ys) - min(ys) + 1) == len(points),
          'paddle color forms one complete rectangle')
    return sum(xs) / len(xs)



def wait_game_frame(guest, out, name, previous=None):
    # APP LAUNCH OK precedes the next framebuffer flip, especially at O0.
    # Wait for actual rendered paddle evidence while retaining the live window.
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        check(live(guest.text()), 'both FP users remain live while awaiting a game frame')
        frame = screenshot(guest, out, name)
        center = desktop_paddle(frame)
        if center is not None and (previous is None or center > previous + 10):
            return frame
        time.sleep(.08)
    raise AssertionError('No rendered/moving Catch paddle within the live FP window')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default=str(ROOT / 'obj/native-fp-desktop-qemu'))
    parser.add_argument('--diagnostic', action='store_true',
                        help='supplementary QEMU pointer-limited diagnostic, NOT strict acceptance')
    parser.add_argument('--iso', help='exact dedicated ISO to boot')
    args = parser.parse_args()
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'apps.img'
    if disk.exists():
        raise RuntimeError('Choose a new output directory; never overwrite a disk')
    iso_name = 'GTOS-native-fp-diagnostic.iso' if args.diagnostic else 'GTOS-native-fp-test.iso'
    qemu_smoke.BOOT_ISO = Path(args.iso).resolve() if args.iso else ROOT / iso_name
    if not qemu_smoke.BOOT_ISO.exists():
        raise RuntimeError('Build make ' + iso_name + ' first')
    (out / 'iso.sha256').write_text(hashlib.sha256(qemu_smoke.BOOT_ISO.read_bytes()).hexdigest()
                                  + '  ' + str(qemu_smoke.BOOT_ISO) + '\n')
    (out / 'mode.txt').write_text('POINTER-LIMITED DIAGNOSTIC, NOT STRICT ACCEPTANCE\n'
                                if args.diagnostic else 'STRICT POINTER COMPARISONS\n')
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk)], check=True)
    g = Guest(out, disk, 64, 4, wait_ready=False, trace_interrupts=True)
    (out / 'qemu-command.json').write_text(json.dumps(g.p.args, indent=2) + '\n')
    (out / 'qemu-version.json').write_text(json.dumps(g.call('query-version'), indent=2) + '\n')
    try:
        g.wait('DESKTOP READY', 15)
        check('DESKTOP MODE FRAMEBUFFER' in g.text() and 'FB FORMAT 00000120' in g.text(),
              'modern 32-bit desktop, not legacy fallback')
        if args.diagnostic:
            check(QUALIFIER in g.text(), 'independent IF-clear FLDENV and FXRSTOR omission qualification')
        else:
            check(QUALIFIER not in g.text(), 'strict run has no diagnostic omission qualification')
        check('NATIVE FP DESKTOP TWO USERS READY' in g.text(), 'two real FP ELF users admitted')
        g.wait('NATIVE FP CPL3 FAULT LIVE'); g.wait('NATIVE FP CPL3 PEER LIVE')
        check(live(g.text()), 'both independently seeded FP users reached state validation')
        for _ in range(15):
            g.call('human-monitor-command', {'command-line': 'sendkey 2 10'})
            g.call('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': 1}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': 1}}]})
            time.sleep(.035)
        g.wait('UI HARDWARE')
        g.verify_workers(4, periodic=True)
        check(live(g.text()), 'each AP performs repeated verified work while both FP users are live')
        g.key('3'); g.key('i'); g.wait('APP INSTALL OK')
        g.key('ret'); g.wait('APP LAUNCH OK')
        before = wait_game_frame(g, out, 'concurrent-fp-game-before')
        before_live = live(g.text())
        g.key('right', 350)
        after = wait_game_frame(g, out, 'concurrent-fp-game-after', desktop_paddle(before))
        check(before_live and live(g.text()), 'game input and screenshots precede either FP user exit/fault')
        a, b = paddle(before), paddle(after)
        check(b > a + 10, f'actual Catch paddle moves right with two FP users alive ({a:.1f} -> {b:.1f})')
        (out / 'paddle-positions.json').write_text(json.dumps({'before': a, 'after': b}) + '\n')
        marker = DIAGNOSTIC_MARKER if args.diagnostic else STRICT_MARKER
        g.wait(marker, 25)
        g.wait('NATIVE RUNTIME PASS', 5)
        text = g.text()
        check((STRICT_MARKER not in text) if args.diagnostic else (DIAGNOSTIC_MARKER not in text),
              'strict and diagnostic success labels cannot be confused')
        check(text.index('WORKER PERIODIC PASS') < text.index('NATIVE FP CPL3 FAULT STATE VERIFIED')
              and text.index('APP LAUNCH OK') < text.index('NATIVE FP CPL3 FAULT STATE VERIFIED'),
              'guest log confirms repeated AP work and game launch precede first FP user termination')
        check('BROWSER PLATFORM PROBE PASS ABI1' in text and 'BROWSER PROBE EXIT 00000000' in text,
              'independent browser-platform fixture passes and exits normally')
        check('NATIVE REAPED 00000003' in text, 'exact three-process reap including browser fixture')
        for kind in ('KEYBOARD', 'MOUSE'):
            match = re.search(r'NATIVE FP CPL3 ' + kind + r' IRQS ([0-9A-F]{8})', text)
            check(match and int(match.group(1), 16) > 0, 'kernel verifies CPL3 ' + kind.lower() + ' IRQs')
        g.wait('SCHEDULER RUNTIME PASS')
        g.verify_workers(4, periodic=True)
        g.key('r'); g.wait('APP RESTART OK'); g.key('esc'); g.wait('APP CLOSE OK')
        screenshot(g, out, 'desktop-after-fp-reap')
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
    with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
        shutil.copyfileobj(source, target)
    trace.unlink()
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'check', str(disk)], check=True)
    result = ('NATIVE FP DESKTOP/AP DIAGNOSTIC PASS (POINTER GAPS REMAIN; NOT STRICT OWNERSHIP ACCEPTANCE)'
              if args.diagnostic else 'Strict concurrent FP-native/desktop/AP acceptance passed')
    (out / 'result.txt').write_text(result + '\n')
    print(result + '. Evidence: ' + str(out), flush=True)


if __name__ == '__main__':
    main()

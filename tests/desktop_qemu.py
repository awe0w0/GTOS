#!/usr/bin/env python3
"""Real 800x600 Multiboot desktop acceptance through QMP keyboard/mouse input.
Run after building the default modern ISO. Writes only a newly created test disk.
Screenshots retain native resolution. No desktop state is simulated or patched.
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys
import time
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from qemu_smoke import Guest, check

ACCENT = (110, 223, 192)
PANEL = (24, 35, 47)

class DesktopGuest(Guest):
    def __init__(self, out, image, memory=64, cpus=4):
        out.mkdir(parents=True, exist_ok=True)
        super().__init__(out, image, memory, cpus)
        try:
            self.px, self.py = 770, 16
            time.sleep(.3)
            im = self.screenshot('boot-desktop')
            check('DESKTOP MODE FRAMEBUFFER' in self.text(), 'kernel selected the modern framebuffer renderer')
            check(im.size == (800, 600), 'real RGB framebuffer is 800x600')
            check('PANIC' not in self.text(), 'boot has no kernel panic')
            self.verify_workers(cpus)
        except Exception:
            self.close()
            raise

    def screenshot(self, name):
        path = (self.out / (name + '.ppm')).resolve()
        self.call('screendump', {'filename': str(path)})
        im = Image.open(path).convert('RGB')
        im.save(self.out / (name + '.png'))
        return im

    def move(self, x, y):
        self.mouse(x - self.px, y - self.py)
        self.px, self.py = x, y
        time.sleep(.15)

    def button(self, down):
        self.call('input-send-event', {'events': [
            {'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
        time.sleep(.15)

    def click(self, x, y):
        self.move(x, y)
        self.button(True)
        self.button(False)

    def drag(self, x, y, dx, dy):
        self.move(x, y)
        self.button(True)
        self.move(x + dx, y + dy)
        self.button(False)


def paddle(im):
    # Default game surface spans x=151..694, y=224..479; only the yellow paddle
    # occupies the lower band. The cursor and score text cannot match this color.
    points = [x for y in range(420, 463) for x in range(151, 695)
              if im.getpixel((x, y)) == (238, 223, 131)]
    return sum(points) / len(points) if points else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', default=str(ROOT / 'obj/desktop-qemu-tests'))
    args = ap.parse_args()
    out = pathlib.Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'apps.img'
    if disk.exists():
        raise RuntimeError('Use a new output directory; never overwrite an existing disk')
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk),
                    '--size-mib', '8'], check=True)
    g = DesktopGuest(out / 'boot64', disk)
    try:
        g.key('2')
        g.wait('UI HARDWARE')
        im = g.screenshot('monitor')
        check(im.getpixel((125, 98)) == ACCENT, 'monitor window visibly focused')
        g.drag(225, 116, -40, -28)
        im = g.screenshot('monitor-dragged')
        check(im.getpixel((85, 70)) == ACCENT, 'titlebar drag moves the real window')
        g.drag(648, 487, 60, 20)
        im = g.screenshot('monitor-resized')
        check(im.getpixel((701, 250)) == PANEL, 'resize expands visible window content')
        g.click(654, 88)
        im = g.screenshot('monitor-maximized')
        check(im.getpixel((20, 30)) == ACCENT, 'maximize fills work area below topbar')
        g.click(739, 48)
        im = g.screenshot('monitor-restored')
        check(im.getpixel((85, 70)) == ACCENT, 'restore keeps pre-maximize position')
        g.click(620, 88)
        im = g.screenshot('monitor-minimized')
        check(im.getpixel((85, 70)) != ACCENT, 'minimize hides the window')
        g.click(354, 574)
        im = g.screenshot('monitor-taskbar-restore')
        check(im.getpixel((85, 70)) == ACCENT, 'taskbar restores the minimized window')

        g.click(30, 574)
        g.screenshot('launcher')
        for key in ['a', 'p', 'p']:
            g.key(key)
        g.screenshot('launcher-search')
        g.key('ret')
        g.wait('UI APPS')
        im = g.screenshot('launcher-opened-apps')
        check(im.getpixel((123, 83)) == ACCENT, 'launcher search opens Applications')
        g.key('l')
        for _ in range(3):
            g.key('z')
        g.key('ret')
        empty = g.screenshot('launcher-empty-result')
        g.key('esc')
        dismissed = g.screenshot('launcher-dismissed')
        check(empty.getpixel((26, 200)) != dismissed.getpixel((26, 200)),
              'empty launcher result is safe and Esc dismisses it')

        g.key('4')
        g.key('t')
        g.wait('UI THEME CHANGED')
        im = g.screenshot('light-appearance')
        check(im.getpixel((175, 300)) == (242, 245, 247), 'light theme changes actual pixels')
        g.key('t')
        g.key('esc')
        g.key('3')
        g.key('i')
        g.wait('APP INSTALL OK')
        g.screenshot('installed-catch')
        for operation in ['drag', 'resize']:
            for overlay in ['u', 'l']:
                sx, sy = (203, 101) if operation == 'drag' else (666, 488)
                g.move(sx, sy)
                g.button(True)
                g.key(overlay)
                g.move(sx + 22, sy + 12)
                im = g.screenshot(f'interrupted-{operation}-{overlay}-overlay')
                # Launcher intentionally removes underlying focus decoration.
                # Window shadow position stays stable regardless of focus.
                expected_title = PANEL if overlay == 'l' else ACCENT
                check(im.getpixel((123, 83)) == expected_title and
                      im.getpixel((675, 300)) == (8, 18, 27),
                      f'{overlay} overlay blocks an in-progress window {operation}')
                g.key('esc')
                g.move(sx + 31, sy + 19)
                g.button(False)
                im = g.screenshot(f'interrupted-{operation}-{overlay}-dismissed')
                check(im.getpixel((123, 83)) == ACCENT and
                      im.getpixel((667, 490)) == (163, 182, 200),
                      f'dismissing {overlay} does not restart the old {operation}')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        before = g.screenshot('catch-before-input')
        g.key('right', 350)
        after = g.screenshot('catch-after-input')
        a, b = paddle(before), paddle(after)
        check(a is not None and b is not None and b > a + 10,
              'installed Catch VM paddle responds to real keyboard input')
        g.verify_workers(4, periodic=True)
        g.key('r')
        g.wait('APP RESTART OK')
        g.screenshot('catch-restarted')
        g.click(626, 146)
        g.screenshot('catch-minimized')
        g.click(590, 574)
        im = g.screenshot('catch-taskbar-restore')
        check(im.getpixel((147, 128)) == ACCENT, 'running app restores from taskbar')
        g.key('esc')
        g.wait('APP CLOSE OK')
        g.key('3')
        g.key('u')
        im = g.screenshot('remove-confirmation')
        check(im.getpixel((200, 215)) == PANEL, 'remove displays confirmation')
        g.key('esc')
        check('APP REMOVE OK' not in g.text(), 'cancel does not remove installed package')
        g.screenshot('removal-canceled')
    finally:
        g.close()
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'check', str(disk)], check=True)

    g = DesktopGuest(out / 'reboot64', disk)
    try:
        check('APP STORE COUNT 00000001' in g.text(), 'installed app survives full reboot')
        g.key('3')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        g.screenshot('persistent-app-running')
        g.key('esc')
        g.key('u')
        g.key('ret')
        g.wait('APP REMOVE OK')
        g.screenshot('application-removed')
    finally:
        g.close()

    g = DesktopGuest(out / 'reboot32', disk, 32, 1)
    try:
        check('APP STORE COUNT 00000000' in g.text(), 'confirmed removal survives reboot')
        g.key('i')
        g.wait('APP INSTALL OK')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        g.screenshot('reinstalled-32mib')
    finally:
        g.close()
    g = DesktopGuest(out / 'boot128', disk, 128, 4)
    try:
        check('APP STORE COUNT 00000001' in g.text(), 'high-resolution desktop boots with 128 MiB')
        g.key('2')
        g.screenshot('hardware-128mib')
    finally:
        g.close()

    invalid = out / 'unformatted.img'
    with invalid.open('xb') as file:
        file.truncate(8 * 1024 * 1024)
    before = hashlib.sha256(invalid.read_bytes()).digest()
    g = DesktopGuest(out / 'unformatted', invalid)
    try:
        g.key('i')
        g.wait('APP INSTALL FAILED')
        g.screenshot('unformatted-refused')
    finally:
        g.close()
    check(hashlib.sha256(invalid.read_bytes()).digest() == before,
          'failed install never formats or writes an unformatted disk')
    print('Modern desktop QEMU acceptance passed. Evidence: ' + str(out), flush=True)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Real Chinese desktop, bounded pinyin and preferences acceptance through QMP."""
import argparse
import hashlib
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from desktop_qemu import DesktopGuest, check, paddle


def type_keys(guest, text):
    for key in text:
        guest.key(key)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default=str(ROOT / 'obj/desktop-language-qemu'))
    out = pathlib.Path(parser.parse_args().output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk = out / 'apps.img'
    if disk.exists():
        raise RuntimeError('Use a fresh output directory; never overwrite a test disk')
    subprocess.run([sys.executable, str(ROOT / 'tools/disk.py'), 'create', str(disk),
                    '--size-mib', '8'], check=True)
    g = DesktopGuest(out / 'choose-chinese', disk)
    try:
        check('UI LOCALE en' in g.text(), 'new settings default to English')
        g.key('4')
        g.screenshot('english-language-selector')
        g.click(489, 244)
        g.wait('UI LOCALE zh-CN')
        g.screenshot('chinese-language-selector')
        g.click(489, 336)
        g.wait('UI THEME light')
        im = g.screenshot('chinese-light-theme')
        check(im.getpixel((175, 300)) == (242, 245, 247), 'theme applies alongside Chinese locale')
        g.key('l')
        type_keys(g, 'shi')
        im = g.screenshot('nine-pinyin-candidates')
        for index in range(9):
            x, y = 28 + (index % 3) * 162, 74 + (index // 3) * 26
            check(im.getpixel((x, y)) == (226, 233, 239), f'candidate {index + 1} is visibly inside its cell')
        g.click(400, 137)
        g.screenshot('ninth-candidate-selected')
        g.key('backspace')
        type_keys(g, 'yingyong')
        g.key('spc')
        g.screenshot('committed-chinese-query')
        g.key('left')
        g.key('backspace')
        type_keys(g, 'ying')
        g.key('spc')
        g.key('right')
        g.screenshot('utf8-cursor-insertion')
        g.key('ret')
        g.wait('UI APPS')
        g.screenshot('chinese-application-search')
        g.key('l')
        type_keys(g, 'shezhi')
        g.key('spc')
        g.key('ret')
        g.screenshot('chinese-settings-alias')
        g.key('l')
        type_keys(g, 'ying')
        g.key('esc')
        g.screenshot('composition-canceled-launcher-open')
        g.key('esc')
        g.key('i')
        g.wait('APP INSTALL OK')
        g.key('l')
        type_keys(g, 'jieqiu')
        g.key('spc')
        g.screenshot('localized-catch-search')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        before = g.screenshot('chinese-catch-before')
        g.key('right', 350)
        after = g.screenshot('chinese-catch-after')
        a, b = paddle(before), paddle(after)
        check(a is not None and b is not None and b > a + 10, 'localized bundled game remains playable')
        g.key('r')
        g.wait('APP RESTART OK')
        g.key('esc')
        g.key('4')
        g.key('c')
        g.key('c')
        g.screenshot('repeat-language-switch')
    finally:
        g.close()

    g = DesktopGuest(out / 'restore-chinese', disk)
    try:
        check('UI LOCALE zh-CN' in g.text() and 'UI THEME light' in g.text(),
              'Chinese and light appearance persist across a full guest reboot')
        check('APP STORE COUNT 00000001' in g.text(), 'settings writes preserve installed package')
        g.screenshot('persisted-chinese-desktop')
        g.key('3')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        g.screenshot('persisted-chinese-game')
        g.key('esc')
        g.key('4')
        g.key('c')
        g.key('t')
        g.screenshot('switched-back-english')
    finally:
        g.close()
    g = DesktopGuest(out / 'restore-english-32mib', disk, 32, 1)
    try:
        check('UI LOCALE en' in g.text() and 'UI THEME dark' in g.text(),
              'switching back to English and dark also persists in 32 MiB guest')
        g.screenshot('english-32mib')
    finally:
        g.close()

    invalid = out / 'unformatted.img'
    with invalid.open('xb') as file:
        file.truncate(8 * 1024 * 1024)
    before = hashlib.sha256(invalid.read_bytes()).digest()
    g = DesktopGuest(out / 'unavailable-settings', invalid)
    try:
        g.key('4')
        g.key('c')
        g.wait('UI LOCALE zh-CN')
        g.key('t')
        g.screenshot('session-only-chinese')
    finally:
        g.close()
    check(hashlib.sha256(invalid.read_bytes()).digest() == before,
          'session language/theme changes never write or format an unowned disk')
    g = DesktopGuest(out / 'session-does-not-persist', invalid)
    try:
        check('UI LOCALE en' in g.text() and 'UI THEME dark' in g.text(),
              'unsaved session changes are truthfully absent after reboot')
    finally:
        g.close()
    print('Chinese desktop QEMU acceptance passed. Evidence: ' + str(out), flush=True)


if __name__ == '__main__':
    main()

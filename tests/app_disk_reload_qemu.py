#!/usr/bin/env python3
"""Real modern desktop disk reload, unchanged media, and foreign-media refusal.

Uses only newly created images. Temporary-read-failure recovery is tested by
desktop_tests.cpp with the real AppStore/desktop/VM and a faultable block device;
this guest test does not claim that it injects a physical ATA read failure.
"""
import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from desktop_qemu import DesktopGuest, paddle
from qemu_smoke import check
from settings_tool_test import record, FIRST_SETTINGS_SECTOR
sys.path.insert(0, str(ROOT / 'tools'))
import disk as disktool


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reload(guest, mouse=False, success=True):
    start = guest.text().count('APP DISK RELOAD ')
    if mouse:
        # Default Applications window: x=103, y=83, w=570.
        guest.click(591, 144)
    else:
        guest.key('r')
    text = guest.text()
    check(text.count('APP DISK RELOAD ') == start + 1,
          ('mouse' if mouse else 'keyboard') + ' initiates one actual reload')
    result = 'OK\n' if success else 'FAILED\n'
    check(text.rsplit('APP DISK RELOAD ', 1)[1].startswith(result),
          'reload reports the actual mount result')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    out = pathlib.Path(parser.parse_args().output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    disk = out / 'apps.img'
    disktool.create(disk)
    image = disktool.Image(disk, writable=True)
    try:
        image.install((ROOT / 'apps/catch.gtapp').read_bytes())
    finally:
        image.close()
    before = digest(disk)
    g = DesktopGuest(out / 'normal', disk)
    try:
        g.key('3')
        g.wait('UI APPS')
        reload(g)
        reload(g, mouse=True)
        g.screenshot('read-only-reload')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        check(paddle(g.screenshot('reloaded-catch-running')) is not None,
              'reloaded package renders its actual game paddle')
        reload_count = g.text().count('APP DISK RELOAD ')
        g.key('r')
        g.wait('APP RESTART OK')
        check(g.text().count('APP DISK RELOAD ') == reload_count,
              'game R still restarts without disk reload')
    finally:
        g.close()
    check(digest(disk) == before, 'successful reload and game use leave disk bytes unchanged')

    # Set the existing persisted locale OFFLINE, then verify the Chinese UI.
    image = disktool.Image(disk, writable=True)
    try:
        # Reuse the existing settings-format test fixture on our offline image.
        image.stream.seek(FIRST_SETTINGS_SECTOR * 512)
        image.stream.write(record(1, 1, 0) * 2)
        image.sync()
    finally:
        image.close()
    chinese_before = digest(disk)
    g = DesktopGuest(out / 'chinese', disk, memory=32, cpus=1)
    try:
        g.key('3')
        reload(g, mouse=True)
        g.screenshot('chinese-reload')
        g.key('ret')
        g.wait('APP LAUNCH OK')
        check(paddle(g.screenshot('chinese-reloaded-game')) is not None,
              'Chinese reload launches the existing application')
    finally:
        g.close()
    check(digest(disk) == chinese_before, 'Chinese reload leaves all image bytes unchanged')

    foreign = out / 'foreign.img'
    disktool.create(foreign)
    with foreign.open('r+b') as stream:
        stream.write(b'X')
    foreign_before = digest(foreign)
    g = DesktopGuest(out / 'foreign', foreign)
    try:
        g.key('3')
        reload(g, success=False)
        reload(g, mouse=True, success=False)
        check('APP INSTALL OK' not in g.text(), 'foreign media never creates a package')
        g.screenshot('foreign-media-rejected')
    finally:
        g.close()
    check(digest(foreign) == foreign_before, 'failed mount never formats or writes foreign media')
    (out / 'qualification.json').write_text(json.dumps(dict(
        passed=True, successful_read_only_reload=True, keyboard_and_mouse=True,
        real_installed_game=True, game_restart_preserved=True, chinese_locale=True,
        foreign_media_refused_without_writes=True, physical_ata_error_injection=False,
        browser_guest_pass=False, healthy_image_sha256=before,
        chinese_image_sha256=chinese_before, foreign_image_sha256=foreign_before), indent=2)+'\n')


if __name__ == '__main__':
    main()

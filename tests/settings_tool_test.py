#!/usr/bin/env python3
"""Existing offline app-image tools preserve the optional settings extension."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import disk
from package import assemble

FIRST_SETTINGS_SECTOR = 259
SETTINGS_END = 261 * 512


def record(generation, locale, theme):
    data = bytearray(512)
    data[:8] = b'GTSET01\0'
    struct.pack_into('<5I', data, 8, 1, 512, generation, locale, theme)
    struct.pack_into('<I', data, 508, disk.crc(data[:508]))
    return bytes(data)


class SettingsToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'data.img'
        disk.create(self.path)

    def tearDown(self):
        self.tmp.cleanup()

    def test_default_image_reserves_blank_settings_without_geometry_change(self):
        raw = self.path.read_bytes()
        self.assertEqual(len(raw), 8 * 1024 * 1024)
        self.assertEqual(struct.unpack_from('<I', raw, 16)[0], 259)
        self.assertEqual(raw[FIRST_SETTINGS_SECTOR * 512:SETTINGS_END], bytes(1024))
        image = disk.Image(self.path)
        self.assertEqual(image.entries, [])
        image.close()

    def test_app_tools_preserve_settings_and_unknown_tail(self):
        settings = record(0xFFFFFFFF, 0, 0) + record(0, 1, 1)
        tail = b'unknown trailing data: must not be claimed by settings or app tools'
        with self.path.open('r+b') as stream:
            stream.seek(FIRST_SETTINGS_SECTOR * 512)
            stream.write(settings + tail)
        before = self.path.read_bytes()[FIRST_SETTINGS_SECTOR * 512:]
        image = disk.Image(self.path, True)
        try:
            for n in range(8):
                package = assemble(dict(id='test%d' % n, title='Test', summary='', code=[['halt']]))
                image.install(package)
                self.assertEqual(image.read('test%d' % n), package)
            for n in range(8):
                image.uninstall('test%d' % n)
        finally:
            image.close()
        self.assertEqual(before, self.path.read_bytes()[FIRST_SETTINGS_SECTOR * 512:])

    def test_app_tool_extent_cannot_write_reserved_settings(self):
        image = disk.Image(self.path, True)
        try:
            for sector in (259, 260, 261):
                with self.assertRaises(disk.StoreError):
                    image.write_sector(sector, record(1, 1, 0))
        finally:
            image.close()

    def test_legacy_minimum_image_still_mounts_app_store(self):
        # 259-sector images remain valid app stores; the settings C++ tests
        # separately prove they cannot be upgraded or written by SettingsStore.
        with self.path.open('r+b') as stream:
            stream.truncate(259 * 512)
        image = disk.Image(self.path, True)
        try:
            package = assemble(dict(id='small', title='Small', summary='', code=[['halt']]))
            image.install(package)
            self.assertEqual(image.read('small'), package)
        finally:
            image.close()
        self.assertEqual(self.path.stat().st_size, 259 * 512)


if __name__ == '__main__':
    unittest.main()

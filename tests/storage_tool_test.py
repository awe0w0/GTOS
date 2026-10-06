#!/usr/bin/env python3
"""Host-image interoperability, safety, and recovery checks (stdlib only)."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import disk
from package import assemble


def app(app_id='test'):
    return assemble(dict(id=app_id, title='Test', summary='', code=[['halt']]))


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'data.img'
        disk.create(self.path, 1)

    def tearDown(self):
        self.tmp.cleanup()

    def test_create_refuses_existing_file(self):
        before = self.path.read_bytes()
        with self.assertRaises(FileExistsError):
            disk.create(self.path)
        self.assertEqual(before, self.path.read_bytes())

    def test_symlinks_refused(self):
        path = Path(self.tmp.name) / 'link.img'
        path.symlink_to(self.path)
        with self.assertRaises(OSError):
            disk.Image(path, True)
        with self.assertRaises(FileExistsError):
            disk.create(path)

    def test_roundtrip_reopen_uninstall_reinstall(self):
        image = disk.Image(self.path, True)
        package = app()
        image.install(package)
        self.assertEqual(image.read('test'), package)
        image.close()
        image = disk.Image(self.path, True)
        self.assertEqual(image.read('test'), package)
        image.uninstall('test')
        self.assertEqual(image.entries, [])
        image.install(package)
        self.assertEqual(image.generation, 3)
        image.close()

    def test_capacity_and_replacement(self):
        image = disk.Image(self.path, True)
        try:
            for i in range(8):
                image.install(app('test%d' % i))
            with self.assertRaises(disk.StoreError):
                image.install(app('ninth'))
            for _ in range(32):
                image.install(app('test0'))
            self.assertEqual(len(image.entries), 8)
        finally:
            image.close()

    def test_invalid_package_changes_nothing(self):
        before = self.path.read_bytes()
        image = disk.Image(self.path, True)
        try:
            data = bytearray(app()); data[135] ^= 1
            with self.assertRaises(ValueError):
                image.install(data)
        finally:
            image.close()
        self.assertEqual(before, self.path.read_bytes())

    def test_corrupt_payload_is_never_read(self):
        image = disk.Image(self.path, True)
        image.install(app())
        sector = disk.DATA_START + image.entries[0]['slot'] * disk.SLOT_SECTORS
        raw = bytearray(image.read_sector(sector)); raw[135] ^= 1
        image.write_sector(sector, raw); image.sync()
        with self.assertRaises(disk.StoreError):
            image.read('test')
        image.close()

    def test_latest_metadata_corruption_falls_back(self):
        image = disk.Image(self.path, True)
        image.install(app())
        sector = image.active + 1
        raw = bytearray(image.read_sector(sector)); raw[508] ^= 1
        image.write_sector(sector, raw); image.sync(); image.close()
        image = disk.Image(self.path)
        self.assertEqual(image.entries, [])
        self.assertEqual(sum(bool(v) for v in image.decoded), 1)
        image.close()

    def test_bad_extent_rejected_even_with_checksum(self):
        image = disk.Image(self.path, True)
        image.install(app())
        sector = image.active + 1
        raw = bytearray(image.read_sector(sector))
        struct.pack_into('<I', raw, 24 + 48, 0xffffffff)
        struct.pack_into('<I', raw, 508, disk.crc(raw[:508]))
        image.write_sector(sector, raw); image.sync(); image.close()
        image = disk.Image(self.path)
        self.assertEqual(image.entries, [])
        image.close()

    def test_blank_file_not_formatted(self):
        blank = Path(self.tmp.name) / 'blank.img'
        blank.write_bytes(bytes(1024 * 1024))
        with self.assertRaises(disk.StoreError):
            disk.Image(blank, True)

    def test_half_range_generations_are_rejected(self):
        raw = bytearray(self.path.read_bytes())
        raw[512:1024] = disk.directory(0)
        raw[1024:1536] = disk.directory(0x80000000)
        self.path.write_bytes(raw)
        with self.assertRaisesRegex(disk.StoreError, 'ambiguous'):
            disk.Image(self.path, True)

    def test_commit_flushes_recovered_head_before_reuse(self):
        image = disk.Image(self.path, True)
        events = []
        sync, write = image.sync, image.write_sector
        def barrier():
            events.append('flush')
            sync()
        def sector(index, data):
            events.append('write')
            write(index, data)
        image.sync, image.write_sector = barrier, sector
        try:
            image.commit([])
            self.assertEqual(events, ['flush', 'write', 'flush'])
        finally:
            image.close()

    def test_failed_recovery_flush_never_writes_directory(self):
        before = self.path.read_bytes()
        image = disk.Image(self.path, True)
        def failed_barrier():
            raise OSError('injected fsync failure')
        image.sync = failed_barrier
        try:
            with self.assertRaises(OSError):
                image.commit([])
        finally:
            image.close()
        self.assertEqual(before, self.path.read_bytes())

    def test_mutating_cli_requires_offline_acknowledgment(self):
        import contextlib
        import io
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            disk.main(['uninstall', str(self.path), 'test'])


if __name__ == '__main__':
    unittest.main()

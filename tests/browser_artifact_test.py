"""Test inventories of compiler output and malformed untrusted metadata."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('browser_artifact', Path(__file__).resolve().parents[1] / 'tools' / 'browser_artifact.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(cls.temp.name)
        source = root / 'probe.s'
        source.write_text('.global _start\n.text\n_start: ud2\n.data\n.long 42\n.bss\n.space 4096\n.section .note.GNU-stack,"",@progbits\n')
        cls.images = {}
        for bits, target in ((32, 'elf_i386'), (64, 'elf_x86_64')):
            obj, binary = root / ('probe%d.o' % bits), root / ('probe%d.elf' % bits)
            subprocess.run(['as', '--%d' % bits, str(source), '-o', str(obj)], check=True)
            subprocess.run(['ld', '-m', target, str(obj), '-o', str(binary)], check=True)
            cls.images[bits] = binary.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_real_freestanding_i386(self):
        report = MODULE.inventory(self.images[32])
        self.assertEqual((report['elf_bits'], report['machine'], report['elf_type']), (32, 'i386', 'EXEC'))
        self.assertIn('zero-filled memory beyond file-backed bytes', report['requirements'])
        self.assertEqual(report['interpreters'], [])
        self.assertEqual(report['guest_loadability'], 'not assessed')

    def test_real_x86_64(self):
        report = MODULE.inventory(self.images[64])
        self.assertEqual((report['elf_bits'], report['machine']), (64, 'x86_64'))
        self.assertGreater(report['load_memory_bytes'], report['load_file_bytes'])

    def test_dynamic_host_binary(self):
        report = MODULE.inventory(Path('/bin/ls').read_bytes())
        self.assertTrue(report['interpreters'])
        self.assertIn('userspace dynamic interpreter', report['requirements'])
        self.assertIn('dynamic-link metadata and relocations', report['requirements'])

    def test_tls_and_wx_metadata(self):
        image = bytearray(self.images[32])
        phoff = struct.unpack_from('<I', image, 28)[0]
        struct.pack_into('<I', image, phoff, 7)
        second = phoff + 32
        struct.pack_into('<I', image, second + 24, 7)
        report = MODULE.inventory(image)
        self.assertIn('thread-local-storage image and thread pointer', report['requirements'])
        self.assertIn('writable executable load segment: review W^X', report['requirements'])

    def test_truncated_inputs(self):
        for end in (0, 4, 15, 16, 51, 52):
            with self.subTest(end=end), self.assertRaises(ValueError):
                MODULE.inventory(self.images[32][:end])

    def test_invalid_fields(self):
        for offset, fmt, value in ((4, '<B', 3), (5, '<B', 2), (6, '<B', 2),
                                   (40, '<H', 51), (42, '<H', 31), (44, '<H', 65535),
                                   (28, '<I', 0xffffffff)):
            image = bytearray(self.images[32])
            struct.pack_into(fmt, image, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                MODULE.inventory(image)

    def test_segment_outside_file(self):
        image = bytearray(self.images[32])
        phoff = struct.unpack_from('<I', image, 28)[0]
        struct.pack_into('<I', image, phoff + 4, len(image))
        struct.pack_into('<I', image, phoff + 16, 1)
        with self.assertRaisesRegex(ValueError, 'exceeds file bounds'):
            MODULE.inventory(image)

    def test_invalid_interpreter(self):
        image = bytearray(self.images[32])
        phoff = struct.unpack_from('<I', image, 28)[0]
        struct.pack_into('<I', image, phoff, 3)
        with self.assertRaisesRegex(ValueError, 'interpreter string'):
            MODULE.inventory(image)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Deterministic split-/usr QEMU environment regression tests; no QEMU needed."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from qemu_runtime import qemu_environment


class QemuEnvironmentTests(unittest.TestCase):
    root = Path('/unpacked runtime/root')

    def libraries(self, env):
        return env['LD_LIBRARY_PATH'].split(os.pathsep)

    def expected(self):
        return [str(self.root / 'lib/x86_64-linux-gnu'),
                str(self.root / 'usr/lib/x86_64-linux-gnu')]

    def test_unset_library_path_includes_both_split_usr_locations(self):
        env = qemu_environment(self.root, {})
        self.assertEqual(self.libraries(env), self.expected())
        self.assertEqual(env['QEMU_MODULE_DIR'],
                         str(self.root / 'usr/lib/x86_64-linux-gnu/qemu'))

    def test_empty_library_path_does_not_add_current_directory(self):
        for value in ('', os.pathsep, os.pathsep * 3):
            with self.subTest(value=value):
                env = qemu_environment(self.root, {'LD_LIBRARY_PATH': value})
                self.assertEqual(self.libraries(env), self.expected())

    def test_inherited_nonempty_entries_retain_order(self):
        inherited = ['/custom/first', '/custom/second', '/custom/first']
        env = qemu_environment(self.root, {'LD_LIBRARY_PATH': os.pathsep.join(inherited)})
        self.assertEqual(self.libraries(env), self.expected() + inherited)

    def test_leading_trailing_and_internal_empty_entries_are_removed(self):
        value = os.pathsep.join(['', '/custom/first', '', '/custom/second', ''])
        env = qemu_environment(self.root, {'LD_LIBRARY_PATH': value})
        self.assertEqual(self.libraries(env), self.expected() + ['/custom/first', '/custom/second'])

    def test_rootless_copy_preserves_other_variables_without_mutation(self):
        original = {'PATH': '/host/bin', 'GTOS_QEMU_DATA_DIR': '/firmware',
                    'LD_LIBRARY_PATH': '/custom', 'QEMU_MODULE_DIR': '/host/modules'}
        before = original.copy()
        env = qemu_environment(self.root, original)
        self.assertEqual(original, before)
        self.assertEqual(env['PATH'], original['PATH'])
        self.assertEqual(env['GTOS_QEMU_DATA_DIR'], original['GTOS_QEMU_DATA_DIR'])
        self.assertIsNot(env, original)

    def test_system_qemu_environment_is_unchanged_copy(self):
        original = {'PATH': '/host/bin', 'LD_LIBRARY_PATH': os.pathsep + '/custom',
                    'QEMU_MODULE_DIR': '/host/modules', 'GTOS_QEMU_DATA_DIR': '/firmware'}
        env = qemu_environment(environ=original)
        self.assertEqual(env, original)
        self.assertIsNot(env, original)
        self.assertEqual(qemu_environment(environ={}), {})

    def test_default_environment_is_copied_not_mutated(self):
        original = {'PATH': '/host/bin', 'LD_LIBRARY_PATH': '/custom'}
        with patch.dict(os.environ, original, clear=True):
            self.assertEqual(qemu_environment(), original)
            env = qemu_environment(self.root)
            self.assertEqual(self.libraries(env), self.expected() + ['/custom'])
            self.assertEqual(dict(os.environ), original)

    def test_relative_runtime_is_made_absolute(self):
        env = qemu_environment(Path('unpacked runtime/root'), {})
        root = Path('unpacked runtime/root').resolve()
        self.assertEqual(self.libraries(env), [str(root / 'lib/x86_64-linux-gnu'),
                                              str(root / 'usr/lib/x86_64-linux-gnu')])

    def test_uses_platform_path_separator(self):
        with patch('qemu_runtime.os.pathsep', ';'):
            env = qemu_environment(self.root, {'LD_LIBRARY_PATH': ';/first;;/second;'})
            self.assertEqual(env['LD_LIBRARY_PATH'], ';'.join(self.expected() + ['/first', '/second']))


if __name__ == '__main__':
    unittest.main()

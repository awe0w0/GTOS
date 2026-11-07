#!/usr/bin/env python3
"""Check the actual GTOS V8 Metagen flags and generated layout."""

import importlib.util
import json
from pathlib import Path
import re
import sys
import unittest


class MetagenTest(unittest.TestCase):
    def test_formal_ninja_edge_supplies_target_and_sdk_flags(self):
        edge = 'obj/v8/v8_base_without_compiler/api-arguments.o'
        flags, cwd = self.flags.get_compile_args_from_ninja(str(self.build), edge)
        self.assertEqual(cwd, str(self.build))
        self.assertIn('--target=i686-unknown-none-elf', flags)
        self.assertIn('-DV8_TARGET_ARCH_IA32', flags)
        self.assertIn('-nostdinc', flags)
        self.assertTrue(any(arg.startswith('-isystem') and
                            arg.endswith('/native_v8_runtime/c-sdk') for arg in flags))
        self.assertNotIn('-MF', flags)
        self.assertNotIn('-o', flags)
        self.assertFalse(any(arg.endswith('/api-arguments.cc') for arg in flags))

    def test_non_compile_edge_and_unknown_object_are_refused(self):
        for edge, diagnostic in [
            ('gen/v8/metagen/instance-types.h', 'unexpected C++ command'),
            ('obj/v8/v8_base_without_compiler/no-such-object.o',
             'could not query Ninja object'),
        ]:
            with self.subTest(edge=edge), self.assertRaisesRegex(
                    RuntimeError, re.escape(diagnostic)):
                self.flags.get_compile_args_from_ninja(str(self.build), edge)

    def test_generated_layout_uses_four_byte_pointers(self):
        directory = self.build / 'gen/v8/metagen'
        layout = json.loads((directory / 'layouts.json').read_text())
        self.assertEqual(layout['config']['pointer_size'], 4)
        self.assertEqual(layout['config']['tagged_size'], 4)
        self.assertGreater(len(layout['classes']), 100)
        self.assertIn('FIRST_TYPE', (directory / 'instance-types.h').read_text())


if __name__ == '__main__':
    view, MetagenTest.build = (Path(arg).resolve() for arg in sys.argv[1:])
    spec = importlib.util.spec_from_file_location(
        'compile_flags', view / 'v8/tools/metagen/compile_flags.py')
    MetagenTest.flags = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(MetagenTest.flags)
    unittest.main(argv=[sys.argv[0]], verbosity=2)

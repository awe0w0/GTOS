#!/usr/bin/env python3
"""Exercise the Content generator launchers and inspect their formal outputs."""

import os
from pathlib import Path
import subprocess
import sys
import unittest


class HostLaunchTest(unittest.TestCase):
    def refuse_relative(self, script, env_var, args):
        result = subprocess.run(
            [sys.executable, '-B', str(self.view / script), *args],
            env=dict(os.environ, **{env_var: 'relative/launcher'}),
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(env_var + ' must be an absolute path', result.stderr)
        self.assertFalse(Path('must-not-exist').exists())

    def test_v8_requires_an_absolute_ia32_runtime(self):
        self.refuse_relative(
            'v8/tools/run.py', 'GTOS_V8_IA32_RUNTIME',
            ['--redirect-stdout', 'must-not-exist', './clang_x86/torque'])

    def test_tint_requires_an_absolute_prebuilt_generator(self):
        self.refuse_relative(
            'third_party/dawn/tools/generate-sources-gn.py',
            'GTOS_TINT_SOURCE_GENERATOR', ['must-not-exist'])

    def test_formal_outputs_contain_generated_declarations(self):
        for path, marker in [
            ('gen/src/tint/lang/core/enums.h', 'enum class'),
            ('gen/v8/builtins-generated/bytecodes-builtins-list.h', 'BYTECODE'),
            ('gen/v8/torque-generated/instance-types.h', 'FIRST_TYPE'),
        ]:
            with self.subTest(path=path):
                self.assertIn(marker, (self.build / path).read_text())


if __name__ == '__main__':
    HostLaunchTest.view, HostLaunchTest.build = (
        Path(arg).resolve() for arg in sys.argv[1:])
    unittest.main(argv=[sys.argv[0]], verbosity=2)

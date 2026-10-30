#!/usr/bin/env python3
"""Deterministic failure-path tests for the strict FP QMP runner."""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
import time
import unittest

spec = importlib.util.spec_from_file_location('native_fp_runner', Path(__file__).with_name('native_fp_smoke_qemu.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class QmpReaderTests(unittest.TestCase):
    def setUp(self):
        read, self.write = os.pipe()
        self.read = os.fdopen(read, 'rb', buffering=0)

    def tearDown(self):
        self.read.close()
        if self.write is not None:
            os.close(self.write)

    def reader(self, seconds=1):
        return runner.QmpReader(self.read, time.monotonic() + seconds)

    def test_multiple_buffered_replies(self):
        os.write(self.write, b'{"event":"STOP"}\n{"return":{}}\n')
        reader = self.reader()
        self.assertEqual(reader.read(), {'event': 'STOP'})
        self.assertEqual(reader.read(), {'return': {}})

    def test_partial_line_cannot_block_deadline(self):
        os.write(self.write, b'{"return":')
        start = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, 'deadline'):
            self.reader(.03).read()
        self.assertLess(time.monotonic() - start, 1)

    def test_missing_greeting_cannot_block_deadline(self):
        with self.assertRaisesRegex(RuntimeError, 'deadline'):
            self.reader(.03).read()

    def test_expired_deadline_rejects_buffered_event_storm(self):
        reader = self.reader(-1)
        reader.buffer = b'{"event":"STOP"}\n' * 100
        with self.assertRaisesRegex(RuntimeError, 'deadline'):
            reader.read()

    def test_eof_is_failure(self):
        os.close(self.write)
        self.write = None
        with self.assertRaisesRegex(RuntimeError, 'closed'):
            self.reader().read()

    def test_bad_json_is_failure(self):
        os.write(self.write, b'{bad}\n')
        with self.assertRaises(ValueError):
            self.reader().read()

    def test_non_object_is_failure(self):
        os.write(self.write, b'[]\n')
        with self.assertRaisesRegex(ValueError, 'not an object'):
            self.reader().read()

    def test_oversized_incomplete_reply_is_failure(self):
        reader = self.reader()
        reader.buffer = b'x' * (1024 * 1024)
        os.write(self.write, b'x')
        with self.assertRaisesRegex(RuntimeError, 'bounded'):
            reader.read()

    def test_crlf_reply_is_accepted(self):
        # QEMU terminates its replies with '\r\n'. The reader splits on '\n', so the line keeps a trailing carriage return, and JSON
        # treating trailing whitespace as nothing is what makes that harmless rather than a parse failure.
        os.write(self.write, b'{"return":{}}\r\n')
        self.assertEqual(self.reader().read(), {'return': {}})

    def test_complete_reply_of_exactly_the_bound_is_accepted(self):
        # The bound is one mebibyte and it is exclusive: a reply that reaches it exactly still gets parsed. The line is placed in the
        # buffer without its newline so the only thing the pipe has to carry is the byte that completes it.
        line = self.reply_of_size(1024 * 1024)
        reader = self.reader()
        reader.buffer = line[:-1]
        os.write(self.write, line[-1:])
        self.assertEqual(reader.read(), self.expected_reply(1024 * 1024))

    def test_bound_wins_over_a_newline_that_arrives_with_it(self):
        # One byte over the bound, and the newline in that same read does not rescue it: the size check runs before the line is
        # handed out, so an oversized reply is refused rather than parsed.
        line = self.reply_of_size(1024 * 1024 + 1)
        reader = self.reader()
        reader.buffer = line[:-1]
        os.write(self.write, line[-1:])
        with self.assertRaisesRegex(RuntimeError, 'bounded'):
            reader.read()

    @staticmethod
    def reply_padding(size):
        # One JSON object line of exactly <size> bytes including its newline: {"pad":"xxx..."} plus '\n'.
        return size - len(b'{"pad":"') - len(b'"}') - 1

    def reply_of_size(self, size):
        return b'{"pad":"' + b'x' * self.reply_padding(size) + b'"}\n'

    def expected_reply(self, size):
        return {'pad': 'x' * self.reply_padding(size)}


def emulator_source():
    """A stand-in emulator that answers nothing and exits 33, like a QEMU that stopped before the greeting.

    It writes the guest's PASS marker only when GTOS_FAKE_QEMU_FRESH=1, so one emitted file serves both the fresh-pass and the
    stale-pass case. The file is written by --emit-emulator because a program cannot be started by the same execution allow-list
    that created it; the caller emits it in one step and starts it in the next.
    """
    return ('#!' + sys.executable + '\n'
        'import os, sys\n'
        'from pathlib import Path\n'
        'log = sys.argv[sys.argv.index("-debugcon") + 1][5:]\n'
        'if os.environ.get("GTOS_FAKE_QEMU_FRESH") == "1":\n'
        '    Path(log).write_text("NATIVE FP SMOKE PASS\\n")\n'
        'sys.exit(33)\n')


class EarlyExitTests(unittest.TestCase):
    def run_emulator(self, directory, write_fresh_pass):
        root = Path(directory)
        # The stand-in is published by the shell before this suite runs: a program this test wrote for itself would bypass the
        # identity the publish step records, so there is no fallback that invents one.
        staged = os.environ.get('GTOS_FAKE_QEMU')
        self.assertTrue(staged, 'GTOS_FAKE_QEMU must name the published stand-in emulator')
        emulator = Path(staged)
        self.assertTrue(emulator.is_file(), 'the staged fake emulator is missing: ' + str(emulator))
        self.assertEqual(emulator.stat().st_mode & 0o222, 0, 'the staged fake emulator must be read-only: ' + str(emulator))
        output = root / 'evidence'
        output.mkdir()
        (output / 'guest.log').write_text('NATIVE FP SMOKE PASS\n')
        environment = dict(os.environ, GTOS_FAKE_QEMU_FRESH='1' if write_fresh_pass else '0')
        result = subprocess.run([sys.executable, str(Path(runner.__file__)),
            '--qemu', str(emulator), '--iso', str(root / 'unused.iso'),
            '--output', str(output)], capture_output=True, text=True, timeout=3, env=environment)
        return result, output

    def test_success_before_greeting_has_initialized_counter(self):
        with tempfile.TemporaryDirectory() as directory:
            result, output = self.run_emulator(directory, True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((output / 'input-events.txt').read_text(), 'QMP input batches: 0\n')

    def test_stale_pass_cannot_turn_failed_launch_into_success(self):
        with tempfile.TemporaryDirectory() as directory:
            result, output = self.run_emulator(directory, False)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual((output / 'guest.log').read_text(), '')
            self.assertFalse((output / 'input-events.txt').exists())


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--emit-emulator':
        target = Path(sys.argv[2])
        target.write_text(emulator_source())
        target.chmod(0o555)
        print('emitted ' + str(target))
        sys.exit(0)
    unittest.main()

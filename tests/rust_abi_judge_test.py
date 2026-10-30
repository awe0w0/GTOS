#!/usr/bin/env python3
"""The ABI judge has to reject what it says it rejects.

`rust_abi_run_check.py` compares ten printed values with an independent reference. Its happy path is exercised by the run stage, but a
judge that has only ever agreed is indistinguishable from one that cannot disagree, so this test feeds it a good log and five broken
ones and requires the right verdict from each:

  good            the reference itself, printed as the image prints it: must pass
  missing line    the last value dropped: must fail on the line count
  wrong value     one value changed: must fail naming that value
  duplicate       one value repeated instead of the next one: must fail (eleven lines)
  not hex         a value replaced by text: must fail as unparsable
  wrong store     the pointer-out store printed as a different word: must fail on the store

Usage: rust_abi_judge_test.py <repo root> [<scratch dir>]
"""
import importlib.util
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

PASSED = []


def check(name, condition, detail=''):
    if condition:
        PASSED.append(name)
        print('PASS ' + name)
    else:
        print('FAIL ' + name + (': ' + detail if detail else ''))
        raise SystemExit(1)


def load_judge(root):
    path = root / 'tests' / 'rust_abi_run_check.py'
    spec = importlib.util.spec_from_file_location('rust_abi_run_check', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    root = Path(sys.argv[1]).resolve()
    scratch = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp(prefix='rust-abi-judge-'))
    scratch.mkdir(parents=True, exist_ok=True)
    judge = load_judge(root)
    expected = judge.reference()
    good = ['%08x' % expected[name] for name in judge.ORDER]
    cases = {
        'good': good,
        'missing': good[:-1],
        'wrong': good[:3] + ['%08x' % ((expected[judge.ORDER[3]] + 1) & 0xFFFFFFFF)] + good[4:],
        'duplicate': good[:2] + [good[1]] + good[2:],
        'nothex': good[:5] + ['zzzznope'] + good[6:],
        'store': good[:8] + ['5aa51235'] + good[9:],
    }
    verdicts = {}
    for name, lines in cases.items():
        log = scratch / (name + '.log')
        log.write_text('\n'.join(lines) + '\n')
        done = subprocess.run([sys.executable, '-B', str(root / 'tests' / 'rust_abi_run_check.py'), '--log', str(log)],
                              cwd=str(root), text=True, capture_output=True)
        verdicts[name] = (done.returncode, done.stdout + done.stderr)
        check('judge run completed for the %s case' % name, done.returncode in (0, 1), str(done.returncode))
    code, output = verdicts['good']
    check('the reference passes its own judge', code == 0 and 'PASS rust ABI run' in output, output[-400:])
    code, output = verdicts['missing']
    check('a missing line is rejected', code == 1 and 'printed 9 lines' in output, output[-400:])
    code, output = verdicts['wrong']
    check('a wrong value is rejected naming the value',
          code == 1 and ('%s is 0x' % judge.ORDER[3]) in output and 'expected 0x' in output, output[-400:])
    code, output = verdicts['duplicate']
    check('a duplicated line is rejected', code == 1 and 'printed 11 lines' in output, output[-400:])
    code, output = verdicts['nothex']
    check('an unparsable value is rejected', code == 1 and 'not a hexadecimal value' in output, output[-400:])
    code, output = verdicts['store']
    check('a store that did not arrive is rejected',
          code == 1 and 'pointer-out store did not arrive' in output, output[-400:])

    # Propagation through the run stage, which is what a caller actually sees. The image here is any published regular file: the
    # launcher named by GTOS_HOST_LAUNCHER is what produces the output, so the three cases below separate the two questions the
    # stage has to keep apart -- did the program exit cleanly, and does what it printed pass the judge.
    published = scratch / 'published'
    published.mkdir()
    image = published / 'image'
    image.write_bytes((root / 'tests' / 'host-runner.sh').read_bytes())
    env = dict(os.environ)
    env.update({'GTOS_HOST_PUBLISH_ROOT': str(published), 'GTOS_PUBLISH_MANIFEST': str(scratch / 'manifest.txt'),
                'GTOS_PUBLISH_RECORD': str(scratch / 'record.txt')})
    staged = subprocess.run(['sh', '-c', '. ./tests/host-runner.sh; stage_binary abi-run-image ' + str(image)],
                            cwd=str(root), env=env, text=True, capture_output=True)
    check('the image was published for the propagation cases', staged.returncode == 0, staged.stdout + staged.stderr)
    def shquote(value):
        return shlex.quote(value)

    good_text = '\n'.join(good) + '\n'
    # The reference fold happens to be zero, so corrupting the last value with "00000000" would have been the right answer rather
    # than a wrong one; the value that is corrupted is therefore derived from the reference instead of written by hand.
    corrupted = '\n'.join(good[:-1] + ['%08x' % ((expected[judge.ORDER[-1]] + 1) & 0xFFFFFFFF)]) + '\n'
    cases = {
        'clean exit, judged output passes': (0, good_text, 0),
        'clean exit, judged output is wrong': (0, corrupted, 1),
        'the program exits non-zero': (9, good_text, 9),
    }
    for name, (exit_code, text, expected_status) in cases.items():
        launcher = scratch / ('launcher-' + str(exit_code) + '-' + str(len(text)) + '.sh')
        payload = scratch / ('payload-' + launcher.stem + '.txt')
        payload.write_text(text)
        launcher.write_text('cat ' + shquote(str(payload)) + '\nexit ' + str(exit_code) + '\n')
        launcher.chmod(0o755)
        # The launcher is named as "sh <script>" rather than as the script itself: a stage runs under an execution allow-list fixed
        # before this file existed, and a new executable inside the work root cannot be on it. Naming the shell, which is already
        # allowed, keeps these cases about the stage's exit propagation rather than about the allow-list.
        env['GTOS_HOST_LAUNCHER'] = 'sh ' + str(launcher)
        env['GTOS_ABI_RUN_LOG'] = str(scratch / ('run-' + launcher.stem + '.log'))
        done = subprocess.run(['sh', 'tests/rust_abi_run_test.sh', 'run'], cwd=str(root), env=env, text=True,
                              capture_output=True)
        check('%s -> stage status %d' % (name, expected_status), done.returncode == expected_status,
              'status %d: %s' % (done.returncode, (done.stdout + done.stderr)[-400:]))

    print('\n%d checks passed' % len(PASSED))
    return 0


if __name__ == '__main__':
    sys.exit(main())

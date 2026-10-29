#!/usr/bin/env python3
"""Boot the diagnosis kernel with the pinned V8 image as its only module, or judge a log that a boot already produced.

Two modes, one judge:

  guest.py                  boot one configuration (environment, see below) and judge what the guest reported
  guest.py judge <log> [status]
                            judge an existing log with the same code the boot path uses, without starting anything; the optional
                            status is the recorded QEMU exit (an integer, or "timeout")

The exit contract is the guest's own: the kernel reports over the debug console and exits through the QEMU debug-exit port, so a
successful lifecycle ends with 33 and a refused one with 35. Neither QEMU's status nor the deadline is read as business success by
itself; the log has to carry every lifecycle record with all of its fields, and the reap has to name both sides it compares.

This observer writes to the debug console only. The QEMU command line disables the serial port, so no serial capture exists for this
diagnosis; that is recorded as such instead of being implied.

Exit status: 0 passed, 1 judged as failed, 2 refused to start or usage, 3 the pinned image is not the expected one.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

# Every record the kernel prints, with the fields the judge requires. A record that is present but incomplete is a failure, not a
# pass with a smaller comparison: an empty or partial line must never make two absent values look equal.
RECORDS = {
    'V8 INITIAL': ['image', 'resident', 'capacity', 'metadata', 'tables'],
    'V8 EXECUTION': ['stage', 'result', 'exit', 'vector', 'error', 'fault', 'calls', 'resident', 'dynamic', 'peer'],
    'V8 WORKLOAD': ['mask', 'gc'],
    'V8 REAP': ['free', 'baseline', 'checks'],
}
PASS_LINE = 'GTOS REAL V8 LARGE HEAP RUN PASS'
FAIL_LINE = 'GTOS REAL V8 LARGE HEAP RUN FAIL'
BOOT_LINE = 'GTOS REAL V8 LARGE HEAP BOOT'
SERIAL = 'not captured: this observer writes to the debug console and the QEMU command line disables the serial port'


def sha256_file(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def parse_records(lines):
    """Return the records by name, and the names that appeared more than once or could not be read."""
    records, repeated, malformed = {}, [], []
    for line in lines:
        for name, fields in RECORDS.items():
            if not line.startswith(name + ' '):
                continue
            if name in records:
                repeated.append(name)
                continue
            values, problem = {}, None
            for word in line[len(name) + 1:].split():
                if '=' not in word:
                    continue
                key, value = word.split('=', 1)
                try:
                    # Every value this observer prints is hexadecimal without a prefix.
                    values[key] = int(value, 16)
                except ValueError:
                    problem = key
            missing = [field for field in fields if field not in values]
            if problem:
                malformed.append('%s.%s is not a number' % (name, problem))
            if missing:
                malformed.append('%s is missing %s' % (name, ', '.join(missing)))
            records[name] = values
    return records, repeated, malformed


def judge(lines, status, timed_out=False):
    """The one production judge: every check below is a fact about the log and the recorded exit."""
    verdict = {'checks': {}, 'reasons': []}
    checks, reasons = verdict['checks'], verdict['reasons']
    records, repeated, malformed = parse_records(lines)
    checks['boot_line'] = sum(1 for line in lines if line.startswith(BOOT_LINE)) == 1
    checks['pass_line_once'] = lines.count(PASS_LINE) == 1
    checks['no_fail_line'] = lines.count(FAIL_LINE) == 0
    checks['no_failed_check'] = not any(line.startswith('FAILED ') for line in lines)
    checks['records_once'] = not repeated
    checks['records_complete'] = not malformed
    checks['exit_is_pass'] = status == 33 and not timed_out
    lifecycle = records.get('V8 EXECUTION', {})
    workload = records.get('V8 WORKLOAD', {})
    reap = records.get('V8 REAP', {})
    checks['stage_reached'] = lifecycle.get('stage') == 4
    checks['result_ok'] = lifecycle.get('result') == 0 and lifecycle.get('exit') == 0
    checks['no_fault'] = lifecycle.get('vector') == 0 and lifecycle.get('fault') == 0 and lifecycle.get('error') == 0
    checks['workload_mask'] = workload.get('mask') == 0x7f
    checks['gc_ran'] = isinstance(workload.get('gc'), int) and workload.get('gc', 0) > 0
    # Both sides must be named before they may be compared; a missing pair is not an equal pair.
    checks['reap_paired'] = isinstance(reap.get('free'), int) and isinstance(reap.get('baseline'), int)
    checks['reap_exact'] = checks['reap_paired'] and reap['free'] == reap['baseline']
    if timed_out:
        reasons.append('QEMU did not finish inside the deadline; a timeout is not a result')
    elif status != 33:
        reasons.append('QEMU exit %s is not the pass code 33' % status)
    for name, present in (('boot line', checks['boot_line']), ('single pass line', checks['pass_line_once']),
                          ('no fail line', checks['no_fail_line']), ('no failed check', checks['no_failed_check']),
                          ('records once', checks['records_once']), ('records complete', checks['records_complete']),
                          ('stage 4', checks['stage_reached']), ('result 0', checks['result_ok']),
                          ('no fault', checks['no_fault']), ('workload mask 0x7f', checks['workload_mask']),
                          ('garbage collection ran', checks['gc_ran']), ('reap names both sides', checks['reap_paired']),
                          ('reap returns to baseline', checks['reap_exact'])):
        if not present:
            reasons.append(name + ' failed')
    reasons.extend(malformed)
    reasons.extend('%s appeared more than once' % name for name in repeated)
    verdict['ok'] = all(checks.values())
    verdict['records'] = records
    return verdict


def expected_image():
    """The pinned image's identity, when the caller names it, so the boot cannot silently drive different bytes."""
    want_sha = os.environ.get('EXPECT_ELF_SHA256', '')
    want_bytes = os.environ.get('EXPECT_ELF_BYTES', '')
    return want_sha, int(want_bytes) if want_bytes else None


def refuse_if_present(*paths):
    for path in paths:
        if path and pathlib.Path(path).exists():
            print('guest: refusing to overwrite existing evidence %s; use a fresh run directory' % path, file=sys.stderr)
            return True
    return False


def judge_mode(argv):
    if len(argv) < 3:
        print('usage: guest.py judge <log> [status]', file=sys.stderr)
        return 2
    log = pathlib.Path(argv[2])
    if not log.is_file():
        print('guest: no log at %s' % log, file=sys.stderr)
        return 2
    raw = argv[3] if len(argv) > 3 else '33'
    timed_out = raw == 'timeout'
    status = None if timed_out else int(raw)
    verdict = judge(log.read_text(errors='replace').splitlines(), status, timed_out)
    verdict['mode'] = 'judge'
    verdict['log'] = {'path': str(log), 'sha256': sha256_file(log)}
    verdict['serial'] = SERIAL
    print(json.dumps(verdict, indent=2, default=str)[:4000])
    return 0 if verdict['ok'] else 1


def boot_mode():
    OUT = pathlib.Path(os.environ['OUT'])
    ELF = pathlib.Path(os.environ['ELF'])
    QEMU = pathlib.Path(os.environ['QEMU'])
    LOADER = pathlib.Path(os.environ['LOADER'])
    QEMU_LIB = os.environ['QEMU_LIB']
    QEMU_DATA = os.environ['QEMU_DATA']
    QEMU_MODULE_DIR = os.environ['QEMU_MODULE_DIR']
    RESULTS = pathlib.Path(os.environ['RESULTS'])
    memory, smp, tag = os.environ['MEMORY'], os.environ['SMP'], os.environ['TAG']
    timeout = int(os.environ.get('TIMEOUT', '420'))
    kernel = OUT / 'v8-kernel.bin'
    log = RESULTS / ('e9-' + tag + '.log')
    stderr_path = RESULTS / ('qemu-' + tag + '.stderr.log')
    result_path = RESULTS / ('result-' + tag + '.json')
    if not kernel.is_file():
        print('guest: no diagnosis kernel at %s' % kernel, file=sys.stderr)
        return 2
    if refuse_if_present(log, stderr_path, result_path):
        return 2
    for tool in (QEMU, LOADER):
        if not tool.is_file():
            print('guest: %s is missing; the supply step stages the tools' % tool, file=sys.stderr)
            return 2
        if not os.stat(str(tool)).st_mode & 0o111:
            print('guest: %s carries no execute bit; the supply step must provide it, this script does not chmod tools' % tool,
                  file=sys.stderr)
            return 2
    want_sha, want_bytes = expected_image()
    elf_sha, elf_bytes = sha256_file(ELF), ELF.stat().st_size
    if (want_sha and elf_sha != want_sha) or (want_bytes is not None and elf_bytes != want_bytes):
        print('guest: the image is not the pinned one (sha256 %s, %d bytes; expected %s, %s bytes)' %
              (elf_sha, elf_bytes, want_sha or 'any', want_bytes), file=sys.stderr)
        return 3

    environment = dict(os.environ)
    environment['LD_LIBRARY_PATH'] = QEMU_LIB + (':' + environment['LD_LIBRARY_PATH'] if environment.get('LD_LIBRARY_PATH') else '')
    environment['QEMU_MODULE_DIR'] = QEMU_MODULE_DIR
    command = [str(LOADER), '--library-path', QEMU_LIB, str(QEMU), '-L', QEMU_DATA, '-machine', 'pc', '-accel', 'tcg', '-cpu', 'max',
               '-m', memory, '-smp', smp, '-rtc', 'base=2026-10-30T02:30:00,clock=vm',
               '-kernel', str(kernel), '-initrd', str(ELF), '-icount', 'shift=0,align=off,sleep=off',
               '-nic', 'none', '-display', 'none', '-monitor', 'none', '-serial', 'none',
               '-debugcon', 'file:' + str(log), '-global', 'isa-debugcon.iobase=0xe9',
               '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
    started = time.time()
    try:
        result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=timeout)
        status, timed_out, stderr = result.returncode, False, result.stderr
    except subprocess.TimeoutExpired as expired:
        status, timed_out = None, True
        stderr = expired.stderr.decode('utf-8', 'replace') if isinstance(expired.stderr, bytes) else str(expired.stderr or '')
    elapsed = round(time.time() - started, 1)
    stderr_path.write_text(stderr or '')
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    verdict = judge(lines, status, timed_out)
    kernel_status_path = OUT / 'status.json'
    kernel_status = json.loads(kernel_status_path.read_text()) if kernel_status_path.is_file() else {}
    bind = {'elf_sha256': elf_sha, 'elf_bytes': elf_bytes, 'kernel_sha256': sha256_file(kernel),
            'kernel_bytes': kernel.stat().st_size, 'profile': kernel_status.get('profile'),
            'sources_digest': kernel_status.get('sources_digest'), 'entry_symbols_sha256': kernel_status.get('entry_symbols_sha256'),
            'tools_digest': kernel_status.get('tools_digest')}
    state = {'tag': tag, 'memory': memory, 'smp': smp,
             'config': {'memory': memory, 'smp': smp, 'timeout_seconds': timeout},
             'kernel': {'path': str(kernel), 'sha256': bind['kernel_sha256'], 'bytes': bind['kernel_bytes']},
             'kernel_status': {'path': str(kernel_status_path), 'sha256': sha256_file(kernel_status_path) if kernel_status_path.is_file() else None},
             'elf': {'path': str(ELF), 'sha256': elf_sha, 'bytes': elf_bytes, 'matches_expected': not want_sha or elf_sha == want_sha},
             'tools': {'qemu': {'path': str(QEMU), 'sha256': sha256_file(QEMU)}, 'loader': {'path': str(LOADER), 'sha256': sha256_file(LOADER)}},
             'bind': bind, 'bind_digest': hashlib.sha256(json.dumps(bind, sort_keys=True).encode()).hexdigest(),
             'command': command, 'qemu_status': status, 'timed_out': timed_out, 'seconds': elapsed,
             'log': {'path': str(log), 'lines': len(lines), 'sha256': sha256_file(log) if log.exists() else None},
             'qemu_stderr': {'path': str(stderr_path), 'sha256': sha256_file(stderr_path), 'tail': (stderr or '')[-2000:]},
             'serial': SERIAL, 'verdict': verdict, 'summary_ok': verdict['ok']}
    result_path.write_text(json.dumps(state, indent=2, default=str) + '\n')
    print(json.dumps(state, indent=2, default=str)[:4000])
    return 0 if verdict['ok'] else 1


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'judge':
        sys.exit(judge_mode(sys.argv))
    sys.exit(boot_mode())

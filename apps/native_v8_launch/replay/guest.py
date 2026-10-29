#!/usr/bin/env python3
"""Boot the diagnosis kernel with the pinned V8 image as its only module, one configuration per invocation.

The exit contract is the guest's own: the kernel reports over the debug console and exits through the QEMU debug-exit port, so a
successful lifecycle ends with 33 and a refused one with 35. Neither QEMU's status nor the deadline is read as business success by
itself; the log has to carry the lifecycle records and the exact reap.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

OUT = pathlib.Path(os.environ['OUT'])
ELF = pathlib.Path(os.environ['ELF'])
QEMU = pathlib.Path(os.environ['QEMU'])
QEMU_LIB = os.environ['QEMU_LIB']
QEMU_DATA = os.environ['QEMU_DATA']
LOADER = pathlib.Path(os.environ['LOADER'])
memory = os.environ['MEMORY']
smp = os.environ['SMP']
tag = os.environ['TAG']
timeout = int(os.environ.get('TIMEOUT', '420'))

kernel = OUT / 'v8-kernel.bin'
if not kernel.is_file():
    raise SystemExit('no diagnosis kernel at ' + str(kernel))
os.chmod(str(QEMU), 0o555)
os.chmod(str(LOADER), 0o555)
log = OUT / ('guest-' + tag + '.log')
if log.exists():
    log.unlink()

environment = dict(os.environ)
environment['LD_LIBRARY_PATH'] = QEMU_LIB + (':' + environment['LD_LIBRARY_PATH'] if environment.get('LD_LIBRARY_PATH') else '')
environment['QEMU_MODULE_DIR'] = os.environ['QEMU_MODULE_DIR']
command = [str(LOADER), '--library-path', QEMU_LIB, str(QEMU), '-L', QEMU_DATA, '-machine', 'pc', '-accel', 'tcg', '-cpu', 'max',
           '-m', memory, '-smp', smp, '-rtc', 'base=2026-10-30T02:30:00,clock=vm',
           '-kernel', str(kernel), '-initrd', str(ELF), '-icount', 'shift=0,align=off,sleep=off',
           '-nic', 'none', '-display', 'none', '-monitor', 'none', '-serial', 'none',
           '-debugcon', 'file:' + str(log), '-global', 'isa-debugcon.iobase=0xe9',
           '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
started = time.time()
try:
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=timeout)
    status, timed_out = result.returncode, False
    stderr = result.stderr[-4000:]
except subprocess.TimeoutExpired as expired:
    status, timed_out = None, True
    stderr = (expired.stderr or b'')[-4000:] if isinstance(expired.stderr, bytes) else str(expired.stderr)[-4000:]
elapsed = round(time.time() - started, 1)
lines = log.read_text(errors='replace').splitlines() if log.exists() else []


def row(prefix):
    for line in lines:
        if line.startswith(prefix):
            fields = {}
            for word in line[len(prefix):].split():
                if '=' in word:
                    name, value = word.split('=', 1)
                    try:
                        # Every value this observer prints is hexadecimal without a prefix, so it is read as hexadecimal.
                        fields[name] = int(value, 16)
                    except ValueError:
                        fields[name] = value
            return fields
    return None


state = {'tag': tag, 'memory': memory, 'smp': smp, 'kernel': str(kernel), 'kernel_sha256': hashlib.sha256(kernel.read_bytes()).hexdigest(),
         'elf': {'path': str(ELF), 'sha256': hashlib.sha256(ELF.read_bytes()).hexdigest()},
         'qemu': {'path': str(QEMU), 'sha256': hashlib.sha256(QEMU.read_bytes()).hexdigest()},
         'command': command, 'qemu_status': status, 'timed_out': timed_out, 'seconds': elapsed,
         'log': {'path': str(log), 'lines': len(lines), 'sha256': hashlib.sha256(log.read_bytes()).hexdigest() if log.exists() else None},
         'stderr_tail': stderr}
state['boot'] = any(line.startswith('GTOS REAL V8 LARGE HEAP BOOT') for line in lines)
state['pass_line'] = lines.count('GTOS REAL V8 LARGE HEAP RUN PASS')
state['fail_line'] = lines.count('GTOS REAL V8 LARGE HEAP RUN FAIL')
state['failed_checks'] = [line for line in lines if line.startswith('FAILED ')]
state['initial'] = row('V8 INITIAL ')
state['execution'] = row('V8 EXECUTION ')
state['workload'] = row('V8 WORKLOAD ')
state['reap'] = row('V8 REAP ')
execution, reap, workload = state['execution'], state['reap'], state['workload']
state['lifecycle'] = bool(execution and reap and workload
                          and execution.get('stage') == 4 and execution.get('result') == 0 and execution.get('exit') == 0
                          and execution.get('vector') == 0 and execution.get('fault') == 0
                          and workload.get('mask') == 127 and workload.get('gc', 0) > 0
                          and reap.get('free') == reap.get('baseline'))
state['summary_ok'] = state['boot'] and state['pass_line'] == 1 and state['fail_line'] == 0 and not state['failed_checks'] \
    and state['lifecycle'] and status == 33
(OUT / ('guest-' + tag + '.json')).write_text(json.dumps(state, indent=2) + '\n')
print(json.dumps({key: state[key] for key in ('tag', 'memory', 'smp', 'boot', 'pass_line', 'fail_line', 'failed_checks',
                                              'initial', 'execution', 'workload', 'reap', 'lifecycle', 'qemu_status',
                                              'timed_out', 'seconds', 'summary_ok')}, indent=2))
sys.exit(0 if state['summary_ok'] else 1)

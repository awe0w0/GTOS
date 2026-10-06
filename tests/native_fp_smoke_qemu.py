#!/usr/bin/env python3
"""Drive real PS/2 interrupts while seeded CPL3 FP registers remain live."""
import argparse
import json
import os
import select
from pathlib import Path
import subprocess
import sys
import time


class QmpReader:
    """Bound QMP reads even when the emulator stops before completing a line."""
    def __init__(self, stream, deadline):
        self.stream, self.deadline, self.buffer = stream, deadline, b''

    def read(self):
        while True:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('QMP deadline expired')
            if b'\n' in self.buffer:
                line, self.buffer = self.buffer.split(b'\n', 1)
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ValueError('QMP reply is not an object')
                return value
            if not select.select([self.stream], [], [], remaining)[0]:
                raise RuntimeError('QMP deadline expired')
            data = os.read(self.stream.fileno(), 65536)
            if not data:
                raise RuntimeError('QMP closed before a complete reply')
            self.buffer += data
            if len(self.buffer) > 1024 * 1024:
                raise RuntimeError('QMP reply exceeds bounded input size')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--data-dir')
    parser.add_argument('--iso', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--cpu', default='max')
    parser.add_argument('--memory', default='64M')
    parser.add_argument('--cpus', default='4')
    parser.add_argument('--panic', action='store_true')
    parser.add_argument('--allow-pointer-limit', action='store_true')
    parser.add_argument('--allow-xm-limit', action='store_true')
    parser.add_argument('--allow-mxcsr-limit', action='store_true')
    args = parser.parse_args()
    def completed(text):
        return ('NATIVE FP SMOKE PASS\n' in text or ((args.allow_pointer_limit or args.allow_xm_limit or args.allow_mxcsr_limit) and
                'NATIVE FP DIAGNOSTIC PASS (EMULATOR GAPS REMAIN)\n' in text))
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    debug = out / 'guest.log'
    debug.write_text('')  # A failed launch must never reuse an old PASS marker.
    command = [args.qemu, '-machine', 'pc', '-accel', 'tcg', '-cpu', args.cpu,
               '-m', args.memory, '-smp', args.cpus, '-cdrom', args.iso, '-boot', 'd',
               '-nic', 'none', '-display', 'none', '-monitor', 'none', '-serial', 'none',
               '-debugcon', 'file:' + str(debug), '-qmp', 'stdio',
               '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
    if args.data_dir:
        command.extend(['-L', args.data_dir])
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'qemu.log').open('w') as qemu_log:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
        injected = 0
        try:
            deadline = time.monotonic() + 50
            reader = QmpReader(process.stdout, deadline)
            greeting = reader.read()
            (out / 'qmp-version.json').write_text(json.dumps(greeting, indent=2) + '\n')

            def call(name, arguments=None):
                request = {'execute': name}
                if arguments:
                    request['arguments'] = arguments
                process.stdin.write(json.dumps(request).encode() + b'\n')
                process.stdin.flush()
                while True:
                    answer = reader.read()
                    if 'return' in answer:
                        return answer['return']
                    if 'error' in answer:
                        raise RuntimeError(str(answer))

            call('qmp_capabilities')
            while process.poll() is None:
                text = debug.read_text(errors='replace') if debug.exists() else ''
                if args.panic and 'NATIVE FP EXPECT CPL0 NM' in text and 'PANIC EXCEPTION vector=00000007' in text:
                    if 'NATIVE FP SMOKE PASS' in text or 'NATIVE FP SMOKE FAIL' in text:
                        raise RuntimeError('guard panic also reported an ordinary completion')
                    print('PASS expected fatal CPL0 #NM (separate negative boot)', flush=True)
                    return 0
                if time.monotonic() > deadline:
                    raise RuntimeError('guest deadline expired')
                if 'NATIVE FP IRQ WINDOW' in text and not args.panic:
                    call('input-send-event', {'events': [
                        {'type': 'key', 'data': {'down': bool(injected % 2 == 0), 'key': {'type': 'qcode', 'data': 'a'}}},
                        {'type': 'rel', 'data': {'axis': 'x', 'value': 2 if injected % 2 else -2}},
                        {'type': 'rel', 'data': {'axis': 'y', 'value': 1}}]})
                    injected += 1
                time.sleep(.015)
            text = debug.read_text(errors='replace') if debug.exists() else ''
            if args.panic or process.returncode != 33 or not completed(text):
                raise RuntimeError(f'guest failed with QEMU exit {process.returncode}')
            (out / 'input-events.txt').write_text(f'QMP input batches: {injected}\n')
            return 0
        except (RuntimeError, OSError, ValueError) as error:
            # The guest may exit successfully while QMP is replying to input.
            if not args.panic:
                try:
                    process.wait(timeout=.2)
                except subprocess.TimeoutExpired:
                    pass
                text = debug.read_text(errors='replace') if debug.exists() else ''
                if process.returncode == 33 and completed(text):
                    (out / 'input-events.txt').write_text(f'QMP input batches: {injected}\n')
                    return 0
            print((out / 'qemu.log').read_text(errors='replace'), file=sys.stderr)
            if debug.exists():
                print(debug.read_text(errors='replace'), file=sys.stderr)
            print(f'FAIL: {error}', file=sys.stderr)
            return 1
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
            process.stdout.close()


if __name__ == '__main__':
    sys.exit(main())

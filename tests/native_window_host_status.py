#!/usr/bin/env python3
"""Summarise the native window host run. Any missing or failed required check is
a failure: the summary never marks a required item green when it did not pass."""
import json
import re
import sys
from pathlib import Path

GUARDED = ['SetAvailable', 'Create', 'Submit', 'Present', 'Close', 'Info',
           'Call', 'RequestClose', 'ResetInput', 'DeliverInput',
           'ReclaimOwner', 'CopySlot', 'ContainsWindow', 'RouteInput',
           'ResetWindowInput', 'RequestWindowClose', 'OwnerOfHandle']


def required(condition):
    """One required item. A missing artefact is a failure, not a skip."""
    return bool(condition)


def main():
    repo, out, compiler, shell_status = sys.argv[1:]
    out = Path(out)
    shell_status = int(shell_status)
    checks = {}

    wire = out / 'run-c11.log'
    checks['c11_wire'] = required(wire.exists() and
                                  'NATIVE WINDOW C11 WIRE PASS' in wire.read_text())
    for level in ('O0', 'O2'):
        log = out / ('run-x64-' + level + '.log')
        checks['x64_' + level] = required(log.exists() and
                                          'NATIVE WINDOW HOST PASS' in log.read_text())
    elf_ok = True
    run_ok = True
    for level in ('O0', 'O2'):
        elf = out / ('native-window-host-i386-' + level)
        elf32 = False
        if elf.exists():
            head = elf.read_bytes()[:20]
            elf32 = (head[:6] == b'\x7fELF\x01\x01'
                     and int.from_bytes(head[18:20], 'little') == 3)
        run = out / ('run-i386-' + level + '.log')
        elf_ok = elf_ok and required(elf32)
        run_ok = run_ok and required(run.exists() and
                                     'NATIVE WINDOW HOST PASS' in run.read_text())
    checks['i386_elf32'] = required(elf_ok)
    checks['i386_run'] = required(run_ok)

    undefined_path = out / 'production-undefined-symbols.txt'
    disassembly_path = out / 'production-disassembly.txt'
    heap_free = False
    guard_problems = ['production artefact missing']
    if undefined_path.exists() and disassembly_path.exists():
        undefined = undefined_path.read_text()
        heap_free = not re.search(
            r'operator new|operator delete|\b(?:malloc|calloc|realloc|free)\b', undefined)
        sections = {}
        name = None
        for line in disassembly_path.read_text().splitlines():
            label = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
            if label:
                name = label.group(1)
                sections[name] = []
            elif name is not None:
                sections[name].append(line)
        guard_problems = []
        cold_clones = 0
        for method in GUARDED:
            labels = [label for label in sections
                      if 'gtos::process::NativeWindows::' + method + '(' in label]
            # GCC may outline the error path of a method into a `[clone .cold]`
            # symbol. That is a compiler split of the SAME method, not a second
            # definition, so the primary body is the one without the clone tag.
            primary = [label for label in labels if '[clone' not in label]
            cold = [label for label in labels if '[clone' in label]
            cold_clones += len(cold)
            if len(primary) != 1:
                guard_problems.append('%s:%d' % (method, len(primary)))
                continue
            body = '\n'.join(sections[primary[0]])
            if not (re.search(r'\bcli\b', body) and re.search(r'\bsti\b', body)):
                guard_problems.append(method + ':no-cli-sti')
            # A cold clone must only jump back into the guarded body. If it could
            # return on its own it would be an unguarded exit, so that is refused.
            for label in cold:
                clone = '\n'.join(sections[label])
                if re.search(r'\bret\b', clone) or not re.search(r'\bjmp\b', clone):
                    guard_problems.append('%s:cold-unguarded' % method)
    checks['no_heap_imports'] = required(heap_free)
    checks['production_guard'] = required(not guard_problems)

    all_pass = required(all(checks.values()) and shell_status == 0)
    status = {
        'all_required_checks_pass': all_pass,
        'required_checks': checks,
        'shell_status': shell_status,
        'source': repo,
        'compiler': compiler,
        'scope': 'Actual pure native window bank, wire layout and C11 header check; '
                 'real ProcessAddressSpace checked-copy, native_runtime dispatch, '
                 'desktop composition, GUI closing and owner reaping require the '
                 'actual guest acceptance and are not claimed here. The reclaim '
                 'census (Snapshot) is compiled only into the diagnostic kernel '
                 'variant and is therefore absent from this production object.',
        'guard_methods': GUARDED,
        'guard_problems': guard_problems,
        'guard_cold_clones': cold_clones,
        'optimizations': {'x64': ['O0', 'O2'], 'i386': ['O0', 'O2']},
        'x64_sanitizers': ['address', 'undefined'],
    }
    (out / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
    print('Evidence:', out / 'status.json')
    if not all_pass:
        failed = [name for name, ok in checks.items() if not ok]
        print('REQUIRED CHECK FAILED:', ', '.join(failed) or 'shell status')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

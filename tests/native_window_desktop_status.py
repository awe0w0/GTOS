#!/usr/bin/env python3
"""Summarise the native window desktop run. A missing or failed required item is
a failure; the summary never marks it green. Works for both the full suite and a
single-group targeted run."""
import json
import sys
from pathlib import Path

PASS_TEXT = 'Desktop/framebuffer safety and interaction tests passed'
GROUP_TEXT = 'native window group passed'


def main():
    repo, out, shell_status = sys.argv[1:]
    out = Path(out)
    shell_status = int(shell_status)
    group_file = out / 'group.txt'
    group = group_file.read_text().strip() if group_file.exists() else ''
    checks = {}
    elf = out / 'native-window-desktop-i386'
    elf32 = False
    if elf.exists():
        head = elf.read_bytes()[:20]
        elf32 = head[:6] == b'\x7fELF\x01\x01' and int.from_bytes(head[18:20], 'little') == 3
    checks['desktop_elf32'] = elf32
    run = out / 'run-i386.log'
    text = run.read_text() if run.exists() else ''
    if group:
        checks['group_run'] = GROUP_TEXT in text
    else:
        checks['desktop_run'] = PASS_TEXT in text
    checks['no_failures'] = 'FAIL ' not in text
    exit_file = out / 'native-exit.txt'
    checks['native_exit_zero'] = exit_file.exists() and exit_file.read_text().strip() == '0'
    all_pass = all(checks.values()) and shell_status == 0
    status = {
        'all_required_checks_pass': all_pass,
        'required_checks': checks,
        'group': group or 'full',
        'shell_status': shell_status,
        'native_exit': exit_file.read_text().strip() if exit_file.exists() else 'missing',
        'source': repo,
        'scope': 'Real geometry/render/desktop with the real native window bank as '
                 'provider. Event records are read through the production '
                 'Call(GTOS_SYS_INPUT_READ) entry. Real syscall checked-copy on a '
                 'CPL3 process and guest composition are not claimed here.',
        'log_tail': text.strip().splitlines()[-4:],
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

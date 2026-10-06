#!/usr/bin/env python3
"""Fail closed when new fixed desktop labels lack a bilingual catalog entry."""
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
rows = json.loads((ROOT / 'tools/i18n_catalog.json').read_text())
known = {row['en'] for row in rows}
# Technical identifiers and key names deliberately remain unchanged in both locales.
invariants = {'GTOS', 'BSP', 'MiB', '/', '32-bit x86  /  GTOS', 'EN', 'catch'}
missing = []
for relative in ['src/gui/modern_desktop.cpp', 'src/gui/modern_render.cpp']:
    source = (ROOT / relative).read_text()
    for line_number, line in enumerate(source.splitlines(), 1):
        if line.lstrip().startswith(('#include', '//', 'asm volatile')):
            continue
        for match in re.finditer(r'"(?:\\.|[^"\\])*"', line):
            literal = json.loads(match.group())
            if literal.endswith('\n'):  # Kernel debug trace, not user-interface text.
                continue
            if literal not in known and literal not in invariants:
                missing.append(f'{relative}:{line_number}: {literal!r}')
if missing:
    raise SystemExit('Unmapped fixed desktop text:\n' + '\n'.join(missing))
print(f'PASS desktop catalog inventory: {len(rows)} bilingual entries')

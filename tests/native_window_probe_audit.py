#!/usr/bin/env python3
"""Scalar audit for a consumer ELF linked by ld.lld.

The kernel audit in tools/audit-kernel-instructions.py accepts GNU ld maps only.
This consumer uses the qualified lld linker, so the same checked ELF parser and
the same prohibited-instruction classifier are bound directly to the input
section placements lld actually produced. No section is exempted: a range that
cannot be decoded is a failure, not a skip.

    native_window_probe_audit.py ELF MAP OBJECT...
"""
import importlib.util
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_classifier():
    spec = importlib.util.spec_from_file_location(
        'kernel_scalar_audit', ROOT / 'tools/audit-kernel-instructions.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def audit(elf, map_path, owners):
    module = load_classifier()
    binary = module.Elf(elf)
    inputs = {path.name: module.Elf(path) for path in owners}
    pattern = re.compile(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\d+\s+(.+):\(([^)]+)\)$')
    text = map_path.read_text()
    assert text.startswith('     VMA      LMA     Size Align Out     In      Symbol'), \
        'expected a complete lld map'
    ranges = []
    seen = set()
    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        address, lma, size, owner, section_name = match.groups()
        assert address == lma, 'Unexpected component load address'
        if owner == '<internal>':
            assert section_name in ('.symtab', '.strtab', '.shstrtab') or \
                section_name.startswith('.rodata'), \
                'Unexpected linker-generated input: ' + section_name
            if section_name in ('.symtab', '.strtab', '.shstrtab'):
                metadata = next(s for s in binary.sections if s['name'] == section_name)
                assert not metadata['flags'] & 6, 'Allocated/executable synthetic metadata'
                assert metadata['type'] == (2 if section_name == '.symtab' else 3)
                assert metadata['addr'] == int(address, 16) and metadata['size'] == int(size, 16)
            else:
                # lld merges read-only data atoms into its own placement inside
                # the R+X .text output section, so the containing section cannot
                # be used as the proof. Prove instead that every owner section of
                # this name is non-code and that the placed bytes are exactly the
                # non-code data those owners contributed.
                start = int(address, 16)
                length = int(size, 16)
                mapped = binary.containing(start, length)
                offset = mapped['offset'] + start - mapped['addr']
                placed = binary.data[offset:offset + length]
                assert len(placed) == length, 'Placement outside the file image'
                for obj in inputs.values():
                    for section in obj.sections:
                        if section['name'] != section_name:
                            continue
                        assert not section['flags'] & 4, 'Code input under ' + section_name
                if section_name == '.rodata.cst8':
                    atoms = set()
                    for obj in inputs.values():
                        for section in obj.sections:
                            if section['name'] != section_name:
                                continue
                            data = obj.section_data(section)
                            atoms.update(data[i:i + 8] for i in range(0, len(data), 8))
                    assert length % 8 == 0 and \
                        all(placed[i:i + 8] in atoms for i in range(0, length, 8))
                else:
                    # Mergeable string data: every NUL-terminated atom placed
                    # here must be one the owners actually supplied.
                    atoms = set()
                    for obj in inputs.values():
                        for section in obj.sections:
                            if section['name'] != section_name:
                                continue
                            data = obj.section_data(section)
                            at = 0
                            while at < len(data):
                                end = data.find(b'\0', at)
                                assert end >= 0, 'Unterminated string atom'
                                atoms.add(data[at:end + 1])
                                at = end + 1
                    at = 0
                    while at < length:
                        end = placed.find(b'\0', at)
                        assert end >= 0, 'Unterminated placed string atom'
                        assert placed[at:end + 1] in atoms, \
                            'Placed data was not supplied by a non-code owner'
                        at = end + 1
            continue
        member = re.search(r'\.a\(([^)]+)\)$', owner)
        name = member.group(1) if member else pathlib.Path(owner).name
        assert name in inputs, 'Unproven linked input: ' + owner
        section = next(s for s in inputs[name].sections if s['name'] == section_name)
        assert int(size, 16) == section['size'], 'Input section size changed'
        if not section['flags'] & 4 or not section['size']:
            continue
        key = (name, section_name)
        assert key not in seen, 'Duplicate executable placement'
        seen.add(key)
        start = int(address, 16)
        ranges.append((start, start + section['size']))
    assert ranges, 'No linked executable input sections'
    merged = []
    for start, end in sorted(ranges):
        assert not merged or start >= merged[-1][1], 'Overlapping linked code ranges'
        if merged and start == merged[-1][1]:
            merged[-1] = (merged[-1][0], end)
        else:
            merged.append((start, end))
    count = 0
    for start, end in merged:
        section = binary.containing(start, end - start)
        decoded = subprocess.check_output(
            ['objdump', '-D', '-z', '--no-show-raw-insn', '-m', 'i386',
             '--section=' + section['name'], '--start-address=' + str(start),
             '--stop-address=' + str(end), str(elf)], text=True)
        found = 0
        for line in decoded.splitlines():
            match = re.match(r'^\s*([0-9a-f]+):\s+(.+)$', line)
            if not match:
                continue
            address = int(match.group(1), 16)
            instruction = match.group(2).strip()
            assert start <= address < end
            assert '(bad)' not in instruction and not instruction.startswith('.byte'), \
                'Undecodable code'
            assert not module.touches_fp(instruction), 'Prohibited FP/SIMD instruction: ' + instruction
            found += 1
            count += 1
        assert found, 'Executable range not decoded'
    return dict(pass_=True,
                scope='All lld-placed SHF_EXECINSTR input sections; no exemptions',
                executable_sections=len(ranges), merged_ranges=len(merged),
                instructions=count, input_objects=len(inputs))


def main():
    elf = pathlib.Path(sys.argv[1])
    map_path = pathlib.Path(sys.argv[2])
    owners = [pathlib.Path(name) for name in sys.argv[3:]]
    result = audit(elf, map_path, owners)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())

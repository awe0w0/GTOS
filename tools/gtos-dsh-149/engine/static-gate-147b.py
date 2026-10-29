"""GTOS-DSH-147 P: run the static judge on the real image, on relocated copies and on one-defect mutants.

This file does not decide anything. It loads bytes, calls static-judge-147.judge() for every case, and writes down what the judge
said. The normal image, the relocated image and every mutant go through that one function, so the green light and the red lights are
the same question asked repeatedly rather than two pieces of code that happen to disagree.

Each mutant changes exactly one thing in a copy of the image, and each is kept: the mutated ELF is written into the stage's own
scratch directory and left there, because a negative whose fixture was deleted proves only that something was once refused. The
previous gate deleted its fixtures in a finally block; that happened, it is recorded, and this file does not do it again.

The mutants, each aimed at one obligation:

    entry-point-moved        the header's entry address is changed to an address no symbol occupies
    entry-not-executable     the entry address is moved into a non-executable section
    segment-past-end         a loadable segment is made longer than the file
    wrong-machine            the machine is changed to x86-64
    symbol-undefined         a defined symbol is marked undefined
    symbol-common            a defined symbol is marked COMMON
    multiboot-checksum       one byte of the multiboot header is changed so the three words no longer sum to zero
    multiboot-flags          the multiboot flags word is changed
    multiboot-absent         the magic is removed
    interp-added             a PT_INTERP program header is forged into unused header space
    load-address-moved       the lowest loadable segment is moved off the address the script places the kernel at
    ctor-pointer-wrong       one constructor pointer is replaced with a different executable address
    ctor-order-swapped       two constructor pointers are exchanged
    ctor-pointer-outside     one constructor pointer is aimed outside executable file-backed code
    map-object-removed       the map loses one object it named
    map-object-added         the map gains an object that was not linked
    map-symbol-value         the map's start_ctors disagrees with the image
    section-past-end         a section is made longer than the file

A mutant is a pass when the judge refuses it and the obligation it breaks is among those the judge names. A mutant is a failure when
the judge accepts it, or refuses it for some other reason, because then the mutant has not tested what it claims to test.
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import importlib.util

_spec = importlib.util.spec_from_file_location('static_judge_147', os.path.join(HERE, 'static-judge-147.py'))
judge_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(judge_module)
judge = judge_module.judge
Elf = judge_module.Elf


def load_objects(paths):
    out = []
    for path in paths:
        with open(path, 'rb') as handle:
            data = handle.read()
        elf = Elf(data)
        elf.header['_basename'] = os.path.basename(path)
        out.append(elf)
    return out


def first_symbol_in(data, want_flags):
    """The first defined symbol whose section carries want_flags, used to aim a mutant at real code."""
    elf = Elf(data)
    for symbol in elf.symbols:
        if symbol['shndx'] in (0, 0xFFF1, 0xFFF2) or not symbol['name']:
            continue
        if symbol['shndx'] < len(elf.sections):
            section = elf.sections[symbol['shndx']]
            if section['sh_flags'] & want_flags:
                return symbol
    return None


def build_mutants(data, map_text):
    """Every mutant as (name, bytes, map text, obligation it should break)."""
    elf = Elf(data)
    header = elf.header
    mutants = []

    def add(name, mutated, text, obligation):
        mutants.append({'name': name, 'image': bytes(mutated), 'map': text, 'obligation': obligation})

    def copy():
        return bytearray(data)

    # The entry point.
    buf = copy()
    struct.pack_into('<I', buf, 24, 0xDEADBEEF)
    add('entry-point-moved', buf, map_text, 'entry')

    buf = copy()
    data_section = None
    for section in elf.sections:
        if section['name'] == '.data' and section['sh_size']:
            data_section = section
    if data_section is not None:
        struct.pack_into('<I', buf, 24, data_section['sh_addr'])
        add('entry-not-executable', buf, map_text, 'entry')

    # Segments and the load address.
    buf = copy()
    first_load = next((p for p in elf.phdrs if p['p_type'] == 1), None)
    if first_load is not None:
        struct.pack_into('<I', buf, header['e_phoff'] + first_load['index'] * header['e_phentsize'] + 16, 0xFFFFFF00)
        add('segment-past-end', buf, map_text, 'program headers')

    buf = copy()
    if first_load is not None:
        struct.pack_into('<I', buf, header['e_phoff'] + first_load['index'] * header['e_phentsize'] + 8, 0x0200000)
        add('load-address-moved', buf, map_text, 'program headers')

    # The header.
    buf = copy()
    struct.pack_into('<H', buf, 18, 62)
    add('wrong-machine', buf, map_text, 'header')

    # Symbols.
    def mark_symbol(buf, want_shndx):
        for section in elf.sections:
            if section['sh_type'] != 2 or section['sh_entsize'] != 16:
                continue
            for i in range(section['sh_size'] // 16):
                base = section['sh_offset'] + i * 16
                name_off = struct.unpack_from('<I', buf, base)[0]
                if name_off == 0:
                    continue
                if struct.unpack_from('<H', buf, base + 14)[0] not in (0, 0xFFF1, 0xFFF2):
                    struct.pack_into('<H', buf, base + 14, want_shndx)
                    return True
        return False

    buf = copy()
    if mark_symbol(buf, 0):
        add('symbol-undefined', buf, map_text, 'symbols')
    buf = copy()
    if mark_symbol(buf, 0xFFF2):
        add('symbol-common', buf, map_text, 'symbols')

    # The multiboot header.
    magic_offset = None
    for offset in range(0, min(len(data), 8192) - 12, 4):
        if struct.unpack_from('<I', data, offset)[0] == 0x1BADB002:
            magic_offset = offset
            break
    if magic_offset is not None:
        buf = copy()
        struct.pack_into('<I', buf, magic_offset + 8, (struct.unpack_from('<I', data, magic_offset + 8)[0] + 1) & 0xFFFFFFFF)
        add('multiboot-checksum', buf, map_text, 'multiboot')
        buf = copy()
        struct.pack_into('<I', buf, magic_offset + 4, 3)
        add('multiboot-flags', buf, map_text, 'multiboot')
        buf = copy()
        struct.pack_into('<I', buf, magic_offset, 0x1BADB003)
        add('multiboot-absent', buf, map_text, 'multiboot')

    # A forged PT_INTERP in unused program-header space. The image has three headers; a fourth is not addressable, so the forged
    # header takes the slot of a header the image does not need to keep: the third one, which this link emits for the stack.
    spare = None
    for phdr in elf.phdrs:
        if phdr['p_type'] not in (1,):
            spare = phdr
    if spare is not None:
        buf = copy()
        base = header['e_phoff'] + spare['index'] * header['e_phentsize']
        struct.pack_into('<I', buf, base, 3)          # p_type = PT_INTERP
        struct.pack_into('<I', buf, base + 4, 0)      # p_offset
        struct.pack_into('<I', buf, base + 8, 0)      # p_vaddr
        struct.pack_into('<I', buf, base + 12, 0)     # p_paddr
        struct.pack_into('<I', buf, base + 16, 16)    # p_filesz
        struct.pack_into('<I', buf, base + 20, 16)    # p_memsz
        add('interp-added', buf, map_text, 'load-time dependencies')

    # Sections.
    buf = copy()
    for section in elf.sections:
        if section['sh_type'] == 8 or section['sh_size'] == 0:
            continue
        base = header['e_shoff'] + section['index'] * header['e_shentsize']
        struct.pack_into('<I', buf, base + 20, 0xFFFFFF00)
        add('section-past-end', buf, map_text, 'sections')
        break

    # The constructor table, addressed through the map's own start_ctors value.
    objects_named, map_symbols = judge_module.parse_map(map_text)
    start = map_symbols.get('start_ctors')
    end = map_symbols.get('end_ctors')
    if start is not None and end is not None and end > start:
        section = elf.section_at(start)
        if section is not None and section['sh_type'] != 8:
            offset = section['sh_offset'] + (start - section['sh_addr'])
            span = end - start
            # A pointer that is executable but is not the constructor: the symbol nearest the entry point.
            code = first_symbol_in(data, 0x4)
            if code is not None and span >= 8:
                buf = copy()
                struct.pack_into('<I', buf, offset, code['value'])
                add('ctor-pointer-wrong', buf, map_text, 'constructors')
                buf = copy()
                first = struct.unpack_from('<I', data, offset)[0]
                second = struct.unpack_from('<I', data, offset + 4)[0]
                struct.pack_into('<I', buf, offset, second)
                struct.pack_into('<I', buf, offset + 4, first)
                add('ctor-order-swapped', buf, map_text, 'constructors')
                buf = copy()
                data_sym = first_symbol_in(data, 0x1)
                if data_sym is not None:
                    struct.pack_into('<I', buf, offset, data_sym['value'])
                    add('ctor-pointer-outside', buf, map_text, 'constructors')

    # The map. An object is named on several lines (its sections, then its LOAD line), so removing one line does not remove the
    # object: the first attempt at this mutant was accepted because the object survived on its other lines. Removing an object
    # means removing every line that names it, which is what an object that was never linked would look like.
    lines = map_text.split('\n')
    victim = None
    for line in lines:
        stripped = line.strip()
        if stripped.endswith('.o') and '/producers/' in stripped:
            victim = stripped.split()[-1]
            break
    if victim is not None:
        kept = [l for l in lines if l.strip().split()[-1:] != [victim]]
        add('map-object-removed', copy(), '\n'.join(kept), 'map')
        # An object that was never linked: a line naming an .o the link never saw, in the shape the map uses.
        ghost = ' .text          0x0000000000100000      0x4 ' + victim[:-2] + '-not-linked.o'
        injected = list(lines)
        # It goes inside the map proper, after the heading. Inserting it at the top of the file would put it among the
        # program-property messages, which the parser deliberately skips, and the mutant would then be testing the skip.
        at = 0
        for index, line in enumerate(injected):
            if line.strip() == 'Linker script and memory map':
                at = index + 1
                break
        injected.insert(at, ghost)
        add('map-object-added', copy(), '\n'.join(injected), 'map')
    altered = []
    done = False
    for line in lines:
        if not done and 'start_ctors = .' in line:
            head = line.split('0x')[1].split()[0]
            line = line.replace('0x' + head, '0x%016x' % (int(head, 16) + 4))
            done = True
        altered.append(line)
    if done:
        add('map-symbol-value', copy(), '\n'.join(altered), 'script symbols')

    return mutants


def run(control):
    with open(control['image'], 'rb') as handle:
        data = handle.read()
    with open(control['map'], 'r', encoding='utf-8', errors='replace') as handle:
        map_text = handle.read()
    objects = load_objects(control['objects'])

    expected = dict(control.get('expected') or {})
    expected['object_order'] = [os.path.basename(p) for p in control['objects']]
    expected['object_paths'] = list(control['objects'])
    expected.setdefault('entry_symbol', 'loader')

    real = judge(data, map_text, expected, objects)
    report = {
        'task': control.get('task'),
        'stage': control.get('stage'),
        'image': {'path': control['image'], 'bytes': len(data)},
        'map': {'path': control['map'], 'bytes': len(map_text)},
        'objects': len(objects),
        'real': real,
    }

    # The relocated copy, judged by the same function. This is the third caller, and it exists so that a green verdict cannot be a
    # statement about one path.
    relocated = control.get('relocated_image')
    if relocated:
        with open(relocated, 'rb') as handle:
            relocated_data = handle.read()
        with open(control['relocated_map'], 'r', encoding='utf-8', errors='replace') as handle:
            relocated_map = handle.read()
        moved = judge(relocated_data, relocated_map, expected, objects)
        report['relocated'] = {
            'path': relocated, 'bytes': len(relocated_data),
            'same_bytes': relocated_data == data, 'accepted': moved['accepted'],
            'obligations_failed': moved['obligations_failed'],
            'problems': moved['problems'][:4],
        }

    scratch = control['scratch']
    os.makedirs(scratch, exist_ok=True)
    rows = []
    for mutant in build_mutants(data, map_text):
        path = os.path.join(scratch, mutant['name'] + '.elf')
        with open(path, 'wb') as handle:
            handle.write(mutant['image'])
        map_path = os.path.join(scratch, mutant['name'] + '.map')
        with open(map_path, 'w', encoding='utf-8') as handle:
            handle.write(mutant['map'])
        verdict = judge(mutant['image'], mutant['map'], expected, objects)
        hit = mutant['obligation'] in verdict['obligations_failed']
        rows.append({
            'name': mutant['name'],
            'targets': mutant['obligation'],
            'fixture_image': path,
            'fixture_map': map_path if mutant['map'] != map_text else None,
            'refused': not verdict['accepted'],
            'obligations_failed': verdict['obligations_failed'],
            'hit_target': hit,
            'problems': verdict['problems'][:3],
            'ok': (not verdict['accepted']) and hit,
        })
    report['mutants'] = rows
    report['mutants_ok'] = all(row['ok'] for row in rows)
    report['counts'] = {
        'mutants': len(rows),
        'refused': len([r for r in rows if r['refused']]),
        'hit_target': len([r for r in rows if r['hit_target']]),
        'accepted_by_mistake': len([r for r in rows if not r['refused']]),
        'wrong_reason': len([r for r in rows if r['refused'] and not r['hit_target']]),
    }
    report['ok'] = real['accepted'] and report['mutants_ok'] and (
        report['relocated']['accepted'] if relocated else True)
    return report


def main():
    with open(sys.argv[1], 'r', encoding='utf-8') as handle:
        control = json.load(handle)
    try:
        report = run(control)
    except Exception as error:
        import traceback
        report = {'ok': False, 'accepted': False, 'problems': [{'obligation': 'run', 'why': str(error)}],
                  'traceback': traceback.format_exc()}
    report['stage'] = control.get('stage')
    report['task'] = control.get('task')
    path = control['report_path']
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write('\n')
    print(json.dumps({
        'stage': report.get('stage'),
        'ok': report.get('ok'),
        'real_accepted': (report.get('real') or {}).get('accepted'),
        'real_failed': (report.get('real') or {}).get('obligations_failed'),
        'counts': report.get('counts'),
        'relocated': report.get('relocated'),
        'bad_mutants': [r['name'] + ':' + ','.join(r['obligations_failed']) for r in (report.get('mutants') or []) if not r['ok']],
    }, indent=2))
    return 0 if report.get('ok') else 1


if __name__ == '__main__':
    sys.exit(main())

"""GTOS-DSH-143 P: the static byte judge, as a pure function.

This module decides. It reads bytes and text it is handed, returns a verdict, and does nothing else: it opens no file it was not
given the bytes of, writes nothing, deletes nothing, and starts no process. That separation is the point of the file. The normal
image, every mutated copy and the relocated copy are all judged by the one function below, so a green light on the real image and
a red light on a mutant are answers from the same question rather than from two pieces of code that happen to disagree.

The obligations, each independent of the link that produced the image:

   1. the header names a 32-bit little-endian i386 executable
   2. the multiboot header is where the specification requires it, with flags 7 and a checksum that makes the sum zero
   3. the image asks for nothing at load time: no PT_INTERP and no PT_DYNAMIC
   4. every program header fits inside the file, and the image loads at the address the script places it
   5. every section's bytes fit inside the file
   6. no symbol is COMMON, and no symbol is undefined
   7. the entry point is a defined symbol, inside an executable section, and inside the file-backed part of an executable segment
   8. the map names exactly the objects that were linked, and nothing else
   9. the six symbols the linker script defines hold the values the map says they hold, and the relations the script asserts
      between them actually hold
  10. the constructor table spans exactly the constructors the input objects contribute, in the order the link places them, and
      every pointer in it lands inside executable file-backed code

Obligation 10 is derived from the input objects rather than from the image: each object's .init_array entries are resolved through
their relocations to the symbol each entry names, so the expected list is a fact about the inputs. An image that merely contains
three functions named _GLOBAL__sub_I_* would pass a name-presence check; it cannot pass this one unless its table holds those
three addresses in that order.
"""

import struct

# Program header types.
PT_LOAD = 1
PT_DYNAMIC = 2
PT_INTERP = 3
# Segment flags.
PF_X = 1
# Section header types.
SHT_SYMTAB = 2
SHT_NOBITS = 8
SHT_REL = 9
SHT_INIT_ARRAY = 14
# Section flags.
SHF_EXECINSTR = 0x4
# Symbol section indices.
SHN_UNDEF = 0
SHN_COMMON = 0xFFF2
# The multiboot header.
MULTIBOOT_MAGIC = 0x1BADB002
MULTIBOOT_FLAGS = 7
MULTIBOOT_SEARCH_BYTES = 8192
# The script.
LOAD_ADDRESS = 0x0100000


def _u16(data, off):
    return struct.unpack_from('<H', data, off)[0]


def _u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


class Elf(object):
    """An ELF32 little-endian image or relocatable object, parsed into plain data."""

    def __init__(self, data):
        self.data = data
        if len(data) < 52:
            raise ValueError('too short to hold an ELF32 header')
        if data[0:4] != b'\x7fELF':
            raise ValueError('no ELF magic')
        self.header = {
            'ei_class': data[4], 'ei_data': data[5], 'ei_version': data[6],
            'e_type': _u16(data, 16), 'e_machine': _u16(data, 18), 'e_version': _u32(data, 20),
            'e_entry': _u32(data, 24), 'e_phoff': _u32(data, 28), 'e_shoff': _u32(data, 32),
            'e_flags': _u32(data, 36), 'e_ehsize': _u16(data, 40), 'e_phentsize': _u16(data, 42),
            'e_phnum': _u16(data, 44), 'e_shentsize': _u16(data, 46), 'e_shnum': _u16(data, 48),
            'e_shstrndx': _u16(data, 50),
        }
        h = self.header
        self.phdrs = []
        for i in range(h['e_phnum']):
            base = h['e_phoff'] + i * h['e_phentsize']
            if base + 32 > len(data):
                raise ValueError('program header %d runs past the end of the file' % i)
            self.phdrs.append({
                'index': i, 'p_type': _u32(data, base), 'p_offset': _u32(data, base + 4),
                'p_vaddr': _u32(data, base + 8), 'p_paddr': _u32(data, base + 12),
                'p_filesz': _u32(data, base + 16), 'p_memsz': _u32(data, base + 20),
                'p_flags': _u32(data, base + 24), 'p_align': _u32(data, base + 28),
            })
        self.sections = []
        for i in range(h['e_shnum']):
            base = h['e_shoff'] + i * h['e_shentsize']
            if base + 40 > len(data):
                raise ValueError('section header %d runs past the end of the file' % i)
            self.sections.append({
                'index': i, 'sh_name': _u32(data, base), 'sh_type': _u32(data, base + 4),
                'sh_flags': _u32(data, base + 8), 'sh_addr': _u32(data, base + 12),
                'sh_offset': _u32(data, base + 16), 'sh_size': _u32(data, base + 20),
                'sh_link': _u32(data, base + 24), 'sh_info': _u32(data, base + 28),
                'sh_addralign': _u32(data, base + 32), 'sh_entsize': _u32(data, base + 36),
            })
        names = ['' for _ in self.sections]
        if h['e_shstrndx'] < len(self.sections):
            table = self.sections[h['e_shstrndx']]
            blob = data[table['sh_offset']:table['sh_offset'] + table['sh_size']]
            for section in self.sections:
                end = blob.find(b'\x00', section['sh_name'])
                names[section['index']] = blob[section['sh_name']:end if end >= 0 else len(blob)].decode('latin-1')
        for section, name in zip(self.sections, names):
            section['name'] = name
        self.symbols = []
        for section in self.sections:
            if section['sh_type'] != SHT_SYMTAB or section['sh_entsize'] != 16:
                continue
            strings = self.sections[section['sh_link']] if section['sh_link'] < len(self.sections) else None
            blob = data[strings['sh_offset']:strings['sh_offset'] + strings['sh_size']] if strings else b''
            for i in range(section['sh_size'] // 16):
                base = section['sh_offset'] + i * 16
                if base + 16 > len(data):
                    raise ValueError('symbol %d runs past the end of the file' % i)
                name_off = _u32(data, base)
                end = blob.find(b'\x00', name_off) if name_off < len(blob) else -1
                self.symbols.append({
                    'index': i, 'name': blob[name_off:end].decode('latin-1') if end >= 0 else '',
                    'value': _u32(data, base + 4), 'size': _u32(data, base + 8),
                    'info': data[base + 12], 'other': data[base + 13], 'shndx': _u16(data, base + 14),
                    'symtab': section['index'],
                })

    def section(self, name):
        for section in self.sections:
            if section['name'] == name:
                return section
        return None

    def section_at(self, addr):
        """The section whose address range contains addr, preferring a file-backed one."""
        hits = [s for s in self.sections
                if s['sh_addr'] and s['sh_size'] and s['sh_addr'] <= addr < s['sh_addr'] + s['sh_size']]
        for section in hits:
            if section['sh_type'] != SHT_NOBITS:
                return section
        return hits[0] if hits else None

    def bytes_of(self, section):
        if section['sh_type'] == SHT_NOBITS:
            return b''
        return self.data[section['sh_offset']:section['sh_offset'] + section['sh_size']]

    def symbol(self, name):
        for symbol in self.symbols:
            if symbol['name'] == name and symbol['shndx'] != SHN_UNDEF:
                return symbol
        return None


def parse_map(text):
    """The object files the map names, and the values it gives the script's symbols.

    A map entry names the input section, then the address, then the object it came from. Only the map proper says where each object
    was placed: the file begins with messages about merging program properties, which name objects in an order that has nothing to
    do with the link, so parsing starts at the map's own heading.
    """
    objects = []
    symbols = {}
    lines = text.split('\n')
    start = 0
    for index, line in enumerate(lines):
        if line.strip() == 'Linker script and memory map':
            start = index + 1
            break
    for line in lines[start:]:
        stripped = line.strip()
        if not stripped:
            continue
        # A script symbol assignment reads: 0x0000000000100000                kernel_start = .
        if '=' in stripped and not stripped.startswith('.'):
            head, _, tail = stripped.partition('=')
            tokens = head.split()
            if len(tokens) == 2 and tokens[0].startswith('0x'):
                name = tokens[1]
                try:
                    symbols[name] = int(tokens[0], 16)
                except ValueError:
                    pass
        token = stripped.split()[-1]
        if token.endswith('.o') and token not in objects:
            objects.append(token)
    return objects, symbols


def _symbol_at_section_offset(elf, shndx, offset):
    """The named symbol defined at (section, offset). A constructor entry points here."""
    for symbol in elf.symbols:
        if symbol['shndx'] == shndx and symbol['value'] == offset and symbol['name']:
            return symbol['name']
    return None


def derive_constructors(objects):
    """The constructors the input objects contribute, in the order the link will place them.

    Each object's .init_array entries are zero in the file; what they point at lives in the accompanying relocation. The relocation
    usually names a section symbol with the offset held in the entry itself, so the target is resolved as (section, addend) and then
    looked up in the symbol table. An entry that names a symbol directly is taken as it stands.
    """
    order = []
    detail = []
    for obj in objects:
        entries = []
        # .init_array is placed first by the script, then .init_array.<priority> sorted by priority.
        plain = [s for s in obj.sections if s['name'] == '.init_array']
        priority = sorted([s for s in obj.sections if s['name'].startswith('.init_array.')],
                          key=lambda s: s['name'])
        for section in plain + priority:
            relocs = [s for s in obj.sections if s['sh_type'] == SHT_REL and s['sh_info'] == section['index']]
            blob = obj.bytes_of(section)
            if section['sh_entsize'] != 4 and section['sh_size'] % 4:
                raise ValueError('an .init_array entry is not four bytes wide')
            for slot in range(0, section['sh_size'], 4):
                addend = _u32(blob, slot) if len(blob) >= slot + 4 else 0
                name = None
                for reloc_section in relocs:
                    strings = obj.sections[reloc_section['sh_link']]
                    symtab = None
                    for candidate in obj.sections:
                        if candidate['index'] == reloc_section['sh_link']:
                            symtab = candidate
                    if symtab is None:
                        continue
                    strblob = obj.bytes_of(strings)
                    for index in range(reloc_section['sh_size'] // 8):
                        base = reloc_section['sh_offset'] + index * 8
                        if base + 8 > len(obj.data):
                            break
                        offset, info = struct.unpack_from('<II', obj.data, base)
                        if offset != slot:
                            continue
                        sym_index = info >> 8
                        sym_base = symtab['sh_offset'] + sym_index * 16
                        if sym_base + 16 > len(obj.data):
                            continue
                        name_off = _u32(obj.data, sym_base)
                        shndx = _u16(obj.data, sym_base + 14)
                        end = strblob.find(b'\x00', name_off) if name_off < len(strblob) else -1
                        named = strblob[name_off:end].decode('latin-1') if end >= 0 else ''
                        if named:
                            name = named
                        else:
                            # A section symbol: the entry's own value is the offset into that section.
                            name = _symbol_at_section_offset(obj, shndx, addend)
                        break
                if name is None:
                    raise ValueError('an .init_array entry in %s names nothing resolvable' % obj.header.get('e_type'))
                entries.append({'symbol': name, 'slot': slot})
                order.append(name)
        if entries:
            detail.append({'object': obj.header.get('_basename'), 'entries': entries})
    return order, detail


def _problem(problems, obligation, why, **extra):
    row = {'obligation': obligation, 'why': why}
    row.update(extra)
    problems.append(row)


def judge(image_bytes, map_text, expected, objects):
    """The one decision. Every caller, including every negative, comes through here."""
    problems = []
    report = {}
    elf = Elf(image_bytes)
    header = elf.header
    report['bytes'] = len(image_bytes)
    report['header'] = {k: header[k] for k in ('ei_class', 'ei_data', 'e_type', 'e_machine', 'e_entry', 'e_phnum', 'e_shnum')}

    # 1. The machine this image is for.
    if header['ei_class'] != 1:
        _problem(problems, 'header', 'not a 32-bit image', observed=header['ei_class'])
    if header['ei_data'] != 1:
        _problem(problems, 'header', 'not little-endian', observed=header['ei_data'])
    if header['e_type'] != 2:
        _problem(problems, 'header', 'not an executable', observed=header['e_type'])
    if header['e_machine'] != 3:
        _problem(problems, 'header', 'not an i386 image', observed=header['e_machine'])

    # 2. The multiboot header. The loader reads it from the first 8192 bytes, at a four-byte boundary, and refuses the image if the
    # three words do not sum to zero.
    multiboot = None
    for offset in range(0, min(len(image_bytes), MULTIBOOT_SEARCH_BYTES) - 12, 4):
        if _u32(image_bytes, offset) == MULTIBOOT_MAGIC:
            multiboot = {'offset': offset, 'flags': _u32(image_bytes, offset + 4),
                         'checksum': _u32(image_bytes, offset + 8)}
            break
    report['multiboot'] = multiboot
    if multiboot is None:
        _problem(problems, 'multiboot', 'no multiboot header in the first %d bytes at a four-byte boundary'
                 % MULTIBOOT_SEARCH_BYTES)
    else:
        if multiboot['offset'] >= MULTIBOOT_SEARCH_BYTES:
            _problem(problems, 'multiboot', 'the header is past the window the loader searches', offset=multiboot['offset'])
        if multiboot['flags'] != MULTIBOOT_FLAGS:
            _problem(problems, 'multiboot', 'the flags are not 7', expected=MULTIBOOT_FLAGS, observed=multiboot['flags'])
        total = (MULTIBOOT_MAGIC + multiboot['flags'] + multiboot['checksum']) & 0xFFFFFFFF
        report['multiboot_checksum_sum'] = total
        if total != 0:
            _problem(problems, 'multiboot', 'magic, flags and checksum do not sum to zero', sum=total)

    # 3. Nothing is asked for at load time. A freestanding kernel has no interpreter and no dynamic section.
    for phdr in elf.phdrs:
        if phdr['p_type'] == PT_INTERP:
            _problem(problems, 'load-time dependencies', 'the image asks for an interpreter', index=phdr['index'])
        if phdr['p_type'] == PT_DYNAMIC:
            _problem(problems, 'load-time dependencies', 'the image carries a dynamic section', index=phdr['index'])

    # 4. Segments, and the load address.
    loads = [p for p in elf.phdrs if p['p_type'] == PT_LOAD]
    report['program_headers'] = len(elf.phdrs)
    report['loadable_segments'] = len(loads)
    if not loads:
        _problem(problems, 'program headers', 'no loadable segment')
    for segment in loads:
        if segment['p_offset'] + segment['p_filesz'] > len(image_bytes):
            _problem(problems, 'program headers', 'a segment runs past the end of the file', index=segment['index'],
                     offset=segment['p_offset'], filesz=segment['p_filesz'], bytes=len(image_bytes))
        if segment['p_filesz'] > segment['p_memsz']:
            _problem(problems, 'program headers', 'a segment holds more file bytes than memory', index=segment['index'])
    base = min(p['p_vaddr'] for p in loads) if loads else None
    report['lowest_load_vaddr'] = base
    if base != LOAD_ADDRESS:
        _problem(problems, 'program headers', 'the image does not load where the script places it',
                 expected=LOAD_ADDRESS, observed=base)

    # 5. Sections and their bytes.
    report['sections'] = len(elf.sections)
    for section in elf.sections:
        if section['sh_type'] == SHT_NOBITS:
            continue
        if section['sh_offset'] + section['sh_size'] > len(image_bytes):
            _problem(problems, 'sections', 'a section runs past the end of the file', section=section['name'],
                     offset=section['sh_offset'], size=section['sh_size'])

    # 6. No COMMON, nothing undefined. A COMMON symbol in an executable means the link never resolved a tentative definition.
    common = sorted({s['name'] for s in elf.symbols if s['shndx'] == SHN_COMMON and s['name']})
    report['common_symbols'] = common
    if common:
        _problem(problems, 'symbols', 'the image still carries COMMON symbols', count=len(common), sample=common[:8])
    undefined = sorted({s['name'] for s in elf.symbols if s['shndx'] == SHN_UNDEF and s['name']})
    report['undefined_symbols'] = undefined
    if undefined:
        _problem(problems, 'symbols', 'the image has undefined symbols', count=len(undefined), sample=undefined[:8])

    # 7. The entry point: a defined symbol, in executable code, in the file-backed part of an executable segment. A symbol whose
    # section occupies no file bytes is a promise the loader cannot keep.
    entry = header['e_entry']
    at_entry = [s for s in elf.symbols if s['value'] == entry and s['shndx'] != SHN_UNDEF]
    report['entry'] = entry
    report['entry_symbols'] = [s['name'] for s in at_entry]
    if not at_entry:
        _problem(problems, 'entry', 'no defined symbol sits at the entry point', entry=entry)
    else:
        section = elf.section_at(entry)
        report['entry_section'] = section['name'] if section else None
        if section is None:
            _problem(problems, 'entry', 'the entry point is in no section', entry=entry)
        elif not (section['sh_flags'] & SHF_EXECINSTR):
            _problem(problems, 'entry', 'the entry point is not inside an executable section', section=section['name'])
    want_symbol = expected.get('entry_symbol')
    if want_symbol and want_symbol not in [s['name'] for s in at_entry]:
        _problem(problems, 'entry', 'the entry point is not the symbol the script names',
                 expected=want_symbol, observed=[s['name'] for s in at_entry])
    covering = [p for p in loads if p['p_flags'] & PF_X and p['p_vaddr'] <= entry < p['p_vaddr'] + p['p_memsz']]
    file_backed = [p for p in covering if entry < p['p_vaddr'] + p['p_filesz']]
    report['entry_segment'] = {'covering': [p['index'] for p in covering], 'file_backed': [p['index'] for p in file_backed]}
    if not covering:
        _problem(problems, 'entry', 'the entry point is in no executable segment')
    elif not file_backed:
        _problem(problems, 'entry', 'the entry point is not in the file-backed part of an executable segment',
                 entry=entry, segments=[{'index': p['index'], 'vaddr': p['p_vaddr'], 'filesz': p['p_filesz']} for p in covering])

    # 8. The map names exactly the linked objects.
    objects_named, map_symbols = parse_map(map_text)
    report['map_objects'] = len(objects_named)
    report['map_symbols'] = map_symbols
    want_order = list(expected.get('object_order') or [])
    if want_order:
        got = [o.rsplit('/', 1)[-1] for o in objects_named]
        got_set, want_set = set(got), set(want_order)
        extra = sorted(got_set - want_set)
        missing = sorted(want_set - got_set)
        report['map_order'] = got
        report['map_order_matches_argv'] = got == want_order
        if extra:
            _problem(problems, 'map', 'the map names objects that were not linked', extra=extra[:8])
        if missing:
            _problem(problems, 'map', 'the map does not name every linked object', missing=missing[:8],
                     missing_count=len(missing))
        if got != want_order:
            first = next((i for i, (a, b) in enumerate(zip(got, want_order)) if a != b), 0)
            report['map_order_first_divergence'] = {
                'at': first, 'linked_order': want_order[first] if first < len(want_order) else None,
                'map_order': got[first] if first < len(got) else None,
                'why': 'the linker places input sections by the script, so the multiboot object is placed first'}
        # The map must name each object by the path the link was given, not merely by a matching basename.
        if expected.get('object_paths'):
            by_base = {o.rsplit('/', 1)[-1]: o for o in objects_named}
            for base, path in zip(want_order, expected['object_paths']):
                if base in by_base and by_base[base] != path:
                    _problem(problems, 'map', 'the map names an object at a different path', object=base,
                             expected=path, observed=by_base[base])
                    break

    # 9. The six symbols the script defines: the image's value must be the map's value, and the relations must hold.
    script_symbols = expected.get('script_symbols') or ['kernel_start', 'kernel_readonly_start', 'kernel_readonly_end',
                                                        'start_ctors', 'end_ctors', 'kernel_end']
    observed_script = {}
    for name in script_symbols:
        symbol = elf.symbol(name)
        observed_script[name] = {'image': symbol['value'] if symbol else None, 'map': map_symbols.get(name)}
        if symbol is None:
            _problem(problems, 'script symbols', 'the image defines no %s' % name)
        elif map_symbols.get(name) is None:
            _problem(problems, 'script symbols', 'the map does not give a value for %s' % name)
        elif symbol['value'] != map_symbols[name]:
            _problem(problems, 'script symbols', 'the image and the map disagree about %s' % name,
                     image=symbol['value'], map=map_symbols[name])
    report['script_symbols'] = observed_script
    values = {name: row['image'] for name, row in observed_script.items() if row['image'] is not None}
    if len(values) == len(script_symbols):
        if values['kernel_start'] != LOAD_ADDRESS:
            _problem(problems, 'script relations', 'kernel_start is not the load address',
                     expected=LOAD_ADDRESS, observed=values['kernel_start'])
        if values['kernel_readonly_start'] != values['kernel_start']:
            _problem(problems, 'script relations', 'the readonly range does not begin where the image begins',
                     kernel_start=values['kernel_start'], kernel_readonly_start=values['kernel_readonly_start'])
        if values['kernel_readonly_end'] % 4096:
            _problem(problems, 'script relations', 'the readonly range does not end on a page boundary',
                     observed=values['kernel_readonly_end'])
        if values['kernel_readonly_end'] < values['kernel_readonly_start']:
            _problem(problems, 'script relations', 'the readonly range ends before it begins')
        if values['start_ctors'] != values['kernel_readonly_end']:
            _problem(problems, 'script relations', 'the constructor table does not begin where the readonly range ends',
                     kernel_readonly_end=values['kernel_readonly_end'], start_ctors=values['start_ctors'])
        if values['end_ctors'] < values['start_ctors']:
            _problem(problems, 'script relations', 'the constructor table ends before it begins')
        span = values['end_ctors'] - values['start_ctors']
        if span % 4:
            _problem(problems, 'script relations', 'the constructor table is not a whole number of pointers', span=span)
        if values['kernel_end'] % 4096:
            _problem(problems, 'script relations', 'kernel_end is not page aligned', observed=values['kernel_end'])
        if values['kernel_end'] < values['end_ctors']:
            _problem(problems, 'script relations', 'kernel_end is before the constructor table ends')
        # The image's own sections must fit inside the span the script declares.
        last = max((s['sh_addr'] + s['sh_size'] for s in elf.sections if s['sh_addr'] and s['sh_size']), default=0)
        report['last_section_end'] = last
        if last > values['kernel_end']:
            _problem(problems, 'script relations', 'a section extends past kernel_end',
                     last_section_end=last, kernel_end=values['kernel_end'])
        # The readonly range must actually cover the executable section, or the script's own split is not describing this image.
        text = [s for s in elf.sections if s['sh_flags'] & SHF_EXECINSTR and s['sh_size']]
        for section in text:
            if section['sh_addr'] < values['kernel_readonly_start'] or section['sh_addr'] + section['sh_size'] > values['kernel_readonly_end']:
                _problem(problems, 'script relations', 'an executable section lies outside the readonly range',
                         section=section['name'], start=section['sh_addr'],
                         end=section['sh_addr'] + section['sh_size'],
                         readonly=[values['kernel_readonly_start'], values['kernel_readonly_end']])

    # 10. The constructor table, against the constructors the input objects contribute.
    expected_ctors, ctor_detail = derive_constructors(objects) if objects else ([], [])
    report['constructors_expected'] = expected_ctors
    report['constructors_derived_from'] = ctor_detail
    table = None
    if values.get('start_ctors') is not None and values.get('end_ctors') is not None:
        span = values['end_ctors'] - values['start_ctors']
        start_section = elf.section_at(values['start_ctors'])
        report['ctors_section'] = start_section['name'] if start_section else None
        if start_section is None:
            _problem(problems, 'constructors', 'the constructor table is in no section', start=values['start_ctors'])
        elif start_section['sh_type'] == SHT_NOBITS:
            _problem(problems, 'constructors', 'the constructor table occupies no file bytes', section=start_section['name'])
        elif values['end_ctors'] > start_section['sh_addr'] + start_section['sh_size']:
            _problem(problems, 'constructors', 'the constructor table runs past the end of its section',
                     section=start_section['name'], end=values['end_ctors'],
                     section_end=start_section['sh_addr'] + start_section['sh_size'])
        else:
            offset = start_section['sh_offset'] + (values['start_ctors'] - start_section['sh_addr'])
            blob = image_bytes[offset:offset + span]
            if len(blob) != span:
                _problem(problems, 'constructors', 'the constructor table is not fully inside the file')
            else:
                table = [struct.unpack_from('<I', blob, i)[0] for i in range(0, span, 4)]
    report['constructors_in_image'] = table
    if table is not None and objects:
        if len(table) != len(expected_ctors):
            _problem(problems, 'constructors', 'the table does not hold the constructors the inputs contribute',
                     expected=len(expected_ctors), observed=len(table))
        else:
            for index, (pointer, name) in enumerate(zip(table, expected_ctors)):
                target = elf.symbol(name)
                if target is None:
                    _problem(problems, 'constructors', 'the image does not define an expected constructor',
                             symbol=name, slot=index)
                    continue
                if pointer != target['value']:
                    _problem(problems, 'constructors', 'a table slot does not point at its constructor',
                             slot=index, symbol=name, expected=target['value'], observed=pointer)
                section = elf.section_at(pointer)
                if section is None:
                    _problem(problems, 'constructors', 'a table pointer is in no section', slot=index, pointer=pointer)
                elif not (section['sh_flags'] & SHF_EXECINSTR):
                    _problem(problems, 'constructors', 'a table pointer does not land in executable code',
                             slot=index, pointer=pointer, section=section['name'])
                else:
                    inside = [p for p in loads if p['p_flags'] & PF_X and p['p_vaddr'] <= pointer < p['p_vaddr'] + p['p_filesz']]
                    if not inside:
                        _problem(problems, 'constructors', 'a table pointer is not inside file-backed executable code',
                                 slot=index, pointer=pointer)

    report['problems'] = problems
    report['obligations_failed'] = sorted({p['obligation'] for p in problems})
    report['accepted'] = not problems
    return report

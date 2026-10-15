#!/usr/bin/env python3
"""Bounded, offline IA32 NASM qualification; never execute target objects."""
import ctypes
import hashlib
import json
import os
import pathlib
import shlex
import struct
import subprocess
import sys

ROOT = pathlib.Path('/mnt/f/GTOS-Chromium')
PACKET = ROOT / 'artifacts/native-nasm-20261015-a'
OUT = PACKET / 'qualify-02'
SOURCE = ROOT / 'sources/chromium-full/src'
AOM = SOURCE / 'third_party/libaom/source/libaom'
FIX = PACKET / 'fixtures'
NASM = SOURCE / 'out/gtos-headless-reference-x64/nasm'
LLD = ROOT / 'toolchains/chromium-clang-llvm24-62397f8b-57/bin/ld.lld'
INPUTS = PACKET / 'inputs-01.json'
INPUT_SHA = '23dda18153af496025dbbeb1c62453418f4371b85ec1e20975ebcbf806445e1b'
ENV = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC'}
STATUS = {'passed': False, 'scope': 'NASM ELF32 objects and static relocation, not guest execution or full libaom',
          'commands': [], 'objects': {}}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    result = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def save(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2) + '\n')


def span(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, 'ELF span outside file')
    return data[offset:offset + size]


def unpack(data, fmt, offset):
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt)))


def string(table, offset):
    require(0 <= offset < len(table), 'String offset outside table')
    end = table.find(b'\0', offset)
    require(end >= offset, 'Unterminated ELF string')
    return table[offset:end].decode('ascii')


def host_check():
    data = NASM.read_bytes()
    require(data[:7] == b'\x7fELF\x02\x01\x01', 'Host NASM must be ELF64 LE')
    h = unpack(data, '<HHIQQQIHHHHHH', 16)
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = h
    require(kind in (2, 3) and machine == 62 and version == 1 and ehsize == 64 and
            phsize == 56 and 0 < phnum < 128 and shsize == 64 and 0 < shnum < 256 and shstr < shnum,
            'Unexpected host NASM ELF header')
    programs = [unpack(data, '<IIQQQQQQ', phoff + i * phsize) for i in range(phnum)]
    interpreters = [span(data, p[2], p[5]) for p in programs if p[0] == 3]
    require(interpreters == [b'/lib64/ld-linux-x86-64.so.2\0'], 'Unexpected host interpreter')
    require(all(not (p[0] in (1, 0x6474e551) and p[1] & 1 and p[1] & 2) for p in programs),
            'Host writable executable mapping')
    sections = [unpack(data, '<IIQQQQIIQQ', shoff + i * shsize) for i in range(shnum)]
    dynamic = [s for s in sections if s[1] == 6]
    require(len(dynamic) == 1, 'Expected one host dynamic section')
    d = dynamic[0]
    require(d[6] < shnum and d[9] == 16 and d[5] % 16 == 0, 'Host dynamic table bounds')
    strings = sections[d[6]]
    require(strings[1] == 3, 'Host dynamic string table')
    table = span(data, strings[4], strings[5])
    entries = [unpack(data, '<qQ', off) for off in range(d[4], d[4] + d[5], 16)]
    require(any(tag == 0 for tag, value in entries), 'Unterminated host dynamic table')
    require(not any(tag in (15, 29) for tag, value in entries), 'Host RPATH/RUNPATH forbidden')
    needed = [string(table, value) for tag, value in entries if tag == 1]
    save('host-elf.json', {'sha256': sha(NASM), 'needed': needed, 'interpreter': interpreters[0].decode().rstrip('\0')})
    # The authenticated reference nasm.ninja explicitly links -lpthread.
    # These are host glibc dependencies, not a GTOS pthread implementation.
    require(needed == ['libpthread.so.0', 'libc.so.6'],
            'Host dependency requires review: ' + repr(needed))


def target_check(path, expected_kind=1):
    data = path.read_bytes()
    require(data[:7] == b'\x7fELF\x01\x01\x01', 'Target must be ELF32 little endian')
    h = unpack(data, '<HHIIIIIHHHHHH', 16)
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = h
    require(kind == expected_kind and machine == 3 and version == 1 and ehsize == 52 and
            shsize == 40 and 0 < shnum < 256 and shstr < shnum, 'Target ELF header mismatch')
    sections = [unpack(data, '<IIIIIIIIII', shoff + i * shsize) for i in range(shnum)]
    names_section = sections[shstr]
    require(names_section[1] == 3, 'Missing section string table')
    table = span(data, names_section[4], names_section[5])
    names = [string(table, s[0]) for s in sections]
    for s in sections:
        if s[1] != 8:
            span(data, s[4], s[5])
        require(not (s[2] & 1 and s[2] & 4), 'Writable executable section')
        require(not s[2] & 0x400, 'Unexpected target TLS')
    require('.text' in names and sections[names.index('.text')][5] > 0, 'Empty target text')
    if expected_kind == 1:
        require(phnum == 0 and '.note.GNU-stack' in names, 'Object stack marker required')
        require(sections[names.index('.note.GNU-stack')][2] == 0, 'Executable stack marker')
    else:
        require(phsize == 32 and 0 < phnum < 32, 'Executable program headers')
        programs = [unpack(data, '<IIIIIIII', phoff + i * 32) for i in range(phnum)]
        require(not any(p[0] in (2, 3, 7) for p in programs), 'Dynamic/interpreter/TLS segment forbidden')
        for p in programs:
            span(data, p[1], p[4])
            require(p[4] <= p[5] and not (p[6] & 1 and p[6] & 2), 'Unsafe load segment')
        require(any(p[0] == 0x6474e551 and not p[6] & 1 for p in programs), 'Nonexecuting stack required')
        require(any(p[0] == 1 and p[6] & 1 and p[2] <= entry < p[2] + p[4] for p in programs), 'Entry outside code')
    symtabs = {}
    exports, imports = {}, []
    for index, s in enumerate(sections):
        if s[1] != 2:
            continue
        require(s[9] == 16 and s[5] % 16 == 0 and s[6] < shnum, 'Symbol table bounds')
        st = sections[s[6]]
        require(st[1] == 3, 'Symbol names table type')
        strings = span(data, st[4], st[5])
        symbols = []
        for off in range(s[4], s[4] + s[5], 16):
            name, value, size, info, other, section = unpack(data, '<IIIBBH', off)
            name = string(strings, name)
            require(section < shnum or section in (0xfff1, 0xfff2), 'Symbol section index')
            symbols.append({'name': name, 'value': value, 'section': section})
            if info >> 4 in (1, 2) and name:
                if section == 0:
                    imports.append(name)
                else:
                    require(name not in exports, 'Duplicate export')
                    exports[name] = {'type': info & 15, 'visibility': other & 3, 'value': value}
        symtabs[index] = symbols
    require(len(symtabs) == 1, 'Expected one symbol table')
    relocs = []
    for s in sections:
        require(s[1] != 4, 'Unexpected RELA in IA32 objects')
        if s[1] != 9:
            continue
        require(s[9] == 8 and s[5] % 8 == 0 and s[6] in symtabs and 0 < s[7] < shnum,
                'Relocation table bounds')
        target = sections[s[7]]
        for off in range(s[4], s[4] + s[5], 8):
            offset, info = unpack(data, '<II', off)
            sym, typ = info >> 8, info & 255
            require(sym < len(symtabs[s[6]]) and typ in (1, 2, 9, 10), 'Unexpected IA32 relocation')
            require(offset <= target[5] and 4 <= target[5] - offset, 'Relocation outside target section')
            relocs.append({'offset': offset, 'type': typ, 'symbol': symtabs[s[6]][sym]['name'], 'section': names[s[7]]})
    return {'sha256': sha(path), 'bytes': len(data), 'kind': kind, 'entry': entry,
            'exports': exports, 'imports': sorted(imports), 'relocations': relocs}


def run(label, argv, success=True):
    require(pathlib.Path(argv[0]) in (NASM, LLD), 'Unexpected executable')
    with (OUT / (label + '.stdout')).open('xb') as stdout, (OUT / (label + '.stderr')).open('xb') as stderr:
        result = subprocess.run(argv, cwd=OUT, env=ENV, shell=False, close_fds=True,
                                stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, timeout=60)
    record = {'label': label, 'argv': argv, 'returncode': result.returncode, 'shell': False,
              'stdout_sha256': sha(OUT / (label + '.stdout')), 'stderr_sha256': sha(OUT / (label + '.stderr'))}
    STATUS['commands'].append(record)
    save('status.json', STATUS)
    require((result.returncode == 0) == success, 'Unexpected returncode for ' + label)
    return result.returncode


def assemble(label, source, include_dirs, deps, upstream=False):
    obj, dep = OUT / (label + '.o'), OUT / (label + '.d')
    argv = [str(NASM), '-felf32', '--reproducible']
    argv += ['-I' + str(p) + '/' for p in include_dirs]
    argv += ['-I./', '-I' + str(SOURCE) + '/', '-I' + str(OUT / 'gen') + '/']
    if upstream:
        argv += ['-DCHROMIUM']
    argv += ['-MD', str(dep), '-o', str(obj), str(source)]
    run(label, argv)
    text = dep.read_text().replace('\\\n', ' ')
    target, separator, values = text.partition(':')
    require(separator and shlex.split(target) == [str(obj)], 'Dependency output target mismatch')
    paths = shlex.split(values)
    require(set(paths) == {str(p) for p in deps} and len(paths) == len(set(paths)), 'Dependency closure mismatch: ' + repr(paths))
    for path in paths:
        require(path in MANIFEST['files'], 'Unbound include: ' + path)
    report = target_check(obj)
    report['dependencies'] = paths
    STATUS['objects'][label] = report
    return report


def verify_inputs():
    require(sha(INPUTS) == INPUT_SHA, 'Input manifest changed')
    for name, digest in MANIFEST['files'].items():
        path = pathlib.Path(name)
        require(path.resolve(strict=True) == path and not path.is_symlink() and sha(path) == digest,
                'Input or alias changed: ' + name)


def main():
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site,
            'Python -I -B -S required')
    require(pathlib.Path(__file__).resolve() == PACKET / 'qualify-v2.py' and OUT.resolve() == OUT, 'Fixed paths required')
    require(all(os.environ.get(k) == v for k, v in ENV.items()) and set(os.environ) <= set(ENV) | {'LC_CTYPE'}, 'Minimum environment required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__) and guard['network_disabled'] and guard['landlock_applied'] and
            guard['artifact_execution_denied'] and guard['writable_output'] == str(OUT), 'Isolation guard mismatch')
    libc = ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6')
    require(libc.prctl(39, 0, 0, 0, 0) == 1 and os.getuid() == 0, 'no_new_privs / mapped UID missing')
    for name, expected in guard['namespace_metadata'].items():
        observed = os.stat('/proc/self/ns/' + name)
        require(observed.st_ino == expected['st_ino'] and observed.st_dev == expected['st_dev'], 'Namespace changed')
    verify_inputs()
    host_check()
    (OUT / 'gen').mkdir()
    run('version', [str(NASM), '-v'])
    version = (OUT / 'version.stdout').read_text().strip()
    require(version == 'NASM version 3.01' or version.startswith('NASM version 3.01 '), 'Unexpected NASM version: ' + version)
    STATUS['host_version'] = version
    caller = assemble('caller', FIX / 'caller.asm', [FIX], [FIX / 'caller.asm', FIX / 'constants.inc'])
    provider = assemble('provider', FIX / 'provider.asm', [FIX], [FIX / 'provider.asm'])
    require(set(caller['exports']) == {'GtosNasmCaller'} and caller['imports'] == ['GtosNasmProvider', 'gtos_nasm_value'], 'Caller symbol ABI')
    require({(r['type'], r['symbol']) for r in caller['relocations']} == {(1, 'gtos_nasm_value'), (2, 'GtosNasmProvider')} and len(caller['relocations']) == 2,
            'Caller relocation ABI')
    require(set(provider['exports']) == {'GtosNasmProvider', 'gtos_nasm_value'} and not provider['imports'], 'Provider symbol ABI')
    config = FIX / 'assembly-config/config/aom_config.asm'
    for label, filename, include in (
            ('bilinear', 'aom_subpixel_bilinear_ssse3.asm', 'aom_ports/x86_abi_support.asm'),
            ('eighttap', 'aom_subpixel_8t_ssse3.asm', 'third_party/x86inc/x86inc.asm')):
        source = AOM / 'aom_dsp/x86' / filename
        report = assemble(label, source, [FIX / 'assembly-config', AOM], [source, AOM / include, config], True)
        taps = '2' if label == 'bilinear' else '8'
        expected = {'aom_filter_block1d' + str(width) + '_' + direction + taps + '_ssse3'
                    for width in (4, 8, 16) for direction in ('h', 'v')}
        require(set(report['exports']) == expected, 'Actual libaom exports mismatch: ' + repr(report['exports']))
        require(all(s['type'] == 2 and s['visibility'] == 2 for s in report['exports'].values()), 'Expected hidden function exports')
        require(set(report['imports']) <= {'_GLOBAL_OFFSET_TABLE_'}, 'Unexpected libaom import')
        require(all(r['type'] in (2, 9, 10) for r in report['relocations']), 'Absolute relocation in PIC libaom object')
    linked = OUT / 'caller-provider.elf'
    run('link', [str(LLD), '-m', 'elf_i386', '-static', '--no-undefined', '--build-id=none', '-z', 'noexecstack',
                 '-e', 'GtosNasmCaller', '-o', str(linked), str(OUT / 'caller.o'), str(OUT / 'provider.o')])
    report = target_check(linked, 2)
    require(not report['imports'] and not report['relocations'] and report['entry'] == report['exports']['GtosNasmCaller']['value'], 'Static link unresolved')
    STATUS['static_link'] = report
    run('missing-include', [str(NASM), '-felf32', '--reproducible', '-o', str(OUT / 'missing.o'), str(FIX / 'missing-include.asm')], False)
    require(not (OUT / 'missing.o').exists(), 'Failed assembly emitted an object')
    run('wrong-format', [str(NASM), '-felf64', '--reproducible', '-o', str(OUT / 'wrong-format.o'), str(FIX / 'provider.asm')])
    try:
        target_check(OUT / 'wrong-format.o')
    except RuntimeError as error:
        require(str(error) == 'Target must be ELF32 little endian', 'Wrong-format rejection reason')
        STATUS['wrong_format_rejected'] = str(error)
    else:
        raise RuntimeError('ELF64 object incorrectly accepted')
    verify_inputs()
    for name, digest in guard['executable_files'].items():
        require(sha(name) == digest, 'Executable changed during qualification')
    STATUS.update(passed=True, inputs_unchanged=True, manifest_sha256=INPUT_SHA,
                  missing_include_rejected=True, target_executed=False, full_libaom_qualified=False,
                  content_graph_complete=False, guard_sha256=sha(OUT / 'guard.json'))
    save('status.json', STATUS)


try:
    require(sha(INPUTS) == INPUT_SHA, 'Manifest hash before parsing')
    MANIFEST = json.loads(INPUTS.read_text())
    main()
except Exception as error:
    STATUS['failure'] = type(error).__name__ + ': ' + str(error)
    save('status.json', STATUS)
    raise

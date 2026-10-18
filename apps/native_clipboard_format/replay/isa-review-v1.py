#!/usr/bin/env python3
"""Review every executable byte of five real native clipboard objects offline."""
import ctypes
import hashlib
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
Q = W / 'artifacts/native-clipboard-format-20261018-a'
P = W / 'artifacts/native-content-frontier-20261018-a'
B = W / 'artifacts/native-pma-20261017-a'
OUT = Q / 'isa-01'
COMPILE = Q / 'compile-01/status.json'
COMPILE_SHA = '9dd79f17f71913ddbda48f794aad708bca2990c299b6086ee01385e0b043141c'
FORMAT = P / 'clipboard-format-compile-02'
FORMAT_SHA = 'ae8553d59c9683267df4cf0dd9502906dd4e400cf2449649148cf9c0e9097185'
FORMAT_RELATIVE = 'obj/ui/base/clipboard/libui_base_clipboard_types.clipboard_format_type_aura.o'
TOOL = pathlib.Path('/usr/bin/x86_64-linux-gnu-objdump')
TOOL_SHA = 'afd9c7433c96cb0f37df99cab98fc860bbbe2bed17f37df66254a8778adfc61d'
PYTHON = pathlib.Path('/usr/bin/python3')
PYTHON_SHA = '298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
ENV = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC'}
PREFIXES = {'lock', 'rep', 'repz', 'repnz', 'data16', 'addr16',
            'cs', 'ds', 'es', 'ss', 'fs', 'gs'}
INSTRUCTION = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)(\S.*)$')
state = {'complete': False, 'ISA_review_pass': False, 'target_executed': False,
         'native_clipboard_pass': False, 'browser_pass': False,
         'inputs': {}, 'aliases': {}, 'outputs': {}, 'objects': {}, 'commands': []}
authorized = False
code = 1


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def bind(path, digest, tool=False):
    logical = pathlib.Path(path)
    actual = logical.resolve(strict=True)
    require(actual.is_file() and ((tool and logical in (TOOL, PYTHON)) or W in actual.parents)
            and sha(actual) == digest, 'Bound input changed or escaped: ' + str(logical))
    previous = state['inputs'].get(str(actual))
    require(previous is None or previous == digest, 'Conflicting input digest')
    state['inputs'][str(actual)] = digest
    state['aliases'][str(logical)] = str(actual)


def receipt(path, digest):
    require(re.fullmatch(r'[0-9a-f]{64}', digest) is not None,
            'Receipt SHA must be fixed after independent root review')
    bind(path, digest)
    value = json.loads(pathlib.Path(path).read_text())
    require('failure' not in value, 'Failed prerequisite receipt')
    return value


def emit():
    (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')


def instruction_words(assembly):
    words = assembly.split()
    prefixes = []
    while words and words[0] in PREFIXES:
        prefixes.append(words.pop(0))
    require(words, 'Missing prefixed instruction')
    return prefixes, words[0]


def prior_policy():
    # These are fixed, already accepted objects. No new clipboard mnemonic,
    # register, prefix or byte is ever unioned into the allowed policy.
    reviewed = receipt(B / 'class-isa-review-02.json',
                       '0aee0b36be62b4e3c0e5338a359b38cb94396c395d7dd344204d6ccea7d9db68')
    decoded = receipt(B / 'class-isa-02/status.json',
                      '54c44c24c2eaf1233285b692af2f652e00c78c1067b9cf9fb8df36d49f8e9f50')
    path_review = receipt(B / 'path-isa-review-01.json',
                          '2f0212f9212402a966b76f2a66a053512f2232e7ae6ab4ba9b1764bbb2850cf4')
    bind(B / 'class-isa-v2.py',
         '777401e99ca191fe19d08b5aab037a8df1aa0b40632679615a69435b57c6dd5c')
    require(all(reviewed[key] for key in ('complete', 'ISA_review_pass',
                'IA32_x87_SSE2_only', 'bound_inputs_unchanged'))
            and reviewed['decoded_bytes'] == 62878
            and reviewed['hardware_TLS_sections'] == 0
            and reviewed['init_fini_array_bytes'] == 0
            and decoded['complete'] and decoded['bound_inputs_unchanged']
            and len(decoded['objects']) == 9
            and set(decoded['objects']) == set(reviewed['objects']),
            'Exact previously accepted whole-object class ISA evidence')
    require(path_review['complete'] and path_review['ISA_review_pass']
            and path_review['IA32_i686_x87_SSE2_only']
            and path_review['all_indexed_executable_bytes_decoded_once_and_matched_to_object']
            and path_review['hardware_TLS_sections'] == 0
            and path_review['init_fini_preinit_array_bytes'] == 0
            and path_review['direct_native_syscall_sites'] == 0,
            'Exact previously accepted FilePath ISA evidence')
    allowed = set(path_review['mnemonics'])
    registers = {'%' + name for name in path_review['registers']}
    prefix_pairs = set()
    cs_nops = set()
    count = 0
    for name, item in decoded['objects'].items():
        accepted = reviewed['objects'][name]
        require(item['sha256'] == accepted['sha256']
                and item['disassembly_sha256'] == accepted['disassembly_sha256']
                and item['decoded_bytes'] == accepted['decoded_bytes']
                and len(item['section_bytes']) == accepted['sections']
                and item['complete_executable_section_coverage']
                and item['relocation_types'] == accepted['relocation_types']
                and item['hardware_TLS_sections'] == 0
                and item['init_fini_array_bytes'] == 0,
                'Previously accepted instruction policy provenance changed')
        allowed.update(item['mnemonics'])
        registers.update(accepted['registers'])
        count += item['decoded_bytes']
        log = B / 'class-isa-02' / (name + '.disassembly.txt')
        bind(log, accepted['disassembly_sha256'])
        for line in log.read_text().splitlines():
            match = INSTRUCTION.match(line)
            if not match:
                continue
            prefixes, mnemonic = instruction_words(match[3])
            require(mnemonic in item['mnemonics'], 'Old decoded mnemonic provenance')
            prefix_pairs.add((mnemonic, tuple(prefixes)))
            if '%cs' in match[3] or 'cs' in prefixes:
                raw = bytes.fromhex(match[2])
                require(mnemonic in ('nop', 'nopl', 'nopw')
                        and re.fullmatch(rb'(?:\x66){0,6}\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00', raw),
                        'Only previously reviewed exact CS NOP padding')
                cs_nops.add(raw)
    require(count == reviewed['decoded_bytes'] and cs_nops, 'Complete fixed historical ISA policy')
    allowed.discard('int')
    registers.discard('%cs')
    require(not {'%fs', '%gs'} & registers
            and all(not re.fullmatch(r'%(?:[yz]mm\d+|mm\d+|k\d+|cr\d+|dr\d+)', value)
                    for value in registers), 'Historical register set must retain native restrictions')
    state['prior_policy'] = {'class_review_sha256': state['inputs'][str(B / 'class-isa-review-02.json')],
                             'class_decode_sha256': state['inputs'][str(B / 'class-isa-02/status.json')],
                             'path_review_sha256': state['inputs'][str(B / 'path-isa-review-01.json')],
                             'allowed_mnemonics': sorted(allowed),
                             'allowed_registers': sorted(registers),
                             'allowed_CS_NOP_bytes': sorted(raw.hex() for raw in cs_nops),
                             'direct_software_interrupts_allowed': False,
                             'policy_derived_from_new_objects': False}
    return allowed, registers, prefix_pairs, cs_nops


def cstring(table, offset):
    require(0 <= offset < len(table), 'Bounded ELF string offset')
    end = table.find(b'\0', offset)
    require(end >= offset, 'Terminated ELF string')
    return table[offset:end].decode('ascii')


def object_sections(data):
    require(52 <= len(data) <= 4 * 1024 * 1024, 'Bounded whole object')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    require(header[0][:7] == b'\x7fELF\x01\x01\x01'
            and header[1:4] == (1, 3, 1) and header[4] == 0
            and header[7] == 0 and header[8] == 52 and header[10] == 0
            and header[11] == 40 and 0 < header[13] < header[12]
            and header[6] + 40 * header[12] <= len(data), 'Bounded ELF32 i386 REL object')
    sections = [struct.unpack_from('<10I', data, header[6] + 40 * index)
                for index in range(header[12])]
    strings_section = sections[header[13]]
    require(strings_section[1] == 3 and strings_section[4] + strings_section[5] <= len(data),
            'Bounded section-name table')
    names = data[strings_section[4]:strings_section[4] + strings_section[5]]
    executable = []
    undefined = []
    relocations = {}
    for index, section in enumerate(sections):
        require(section[1] == 8 or section[4] + section[5] <= len(data), 'Bounded complete ELF section')
        require(not (section[2] & 0x400)
                and not (section[1] in (14, 15, 16) and section[5]),
                'No hardware TLS or init/fini/preinit arrays')
        require(section[1] != 4, 'No IA32 RELA')
        label = cstring(names, section[0])
        if label == '.note.GNU-stack':
            require(not (section[2] & 4), 'No executable-stack marker')
        if section[1] == 2:
            require(section[9] == 16 and section[5] % 16 == 0
                    and section[6] < len(sections), 'Bounded whole symbol table')
            strings = sections[section[6]]
            require(strings[1] == 3 and strings[4] + strings[5] <= len(data), 'Bounded symbol-name table')
            symbols = data[strings[4]:strings[4] + strings[5]]
            for at in range(section[4], section[4] + section[5], 16):
                symbol = struct.unpack_from('<IIIBBH', data, at)
                require((symbol[3] & 15) != 6, 'No STT_TLS symbol')
                name = cstring(symbols, symbol[0])
                if symbol[5] == 0 and symbol[3] >> 4 in (1, 2):
                    undefined.append(name)
        if section[1] == 9:
            require(section[9] == 8 and section[5] % 8 == 0
                    and section[6] < len(sections) and 0 < section[7] < len(sections),
                    'Bounded whole IA32 relocation table')
            symbols = sections[section[6]]
            require(symbols[1] == 2 and symbols[9] == 16 and symbols[5] % 16 == 0,
                    'Relocations reference the actual symbol table')
            target = sections[section[7]]
            for at in range(section[4], section[4] + section[5], 8):
                offset, info = struct.unpack_from('<II', data, at)
                kind = info & 255
                require(kind in (1, 2) and offset + 4 <= target[5]
                        and info >> 8 < symbols[5] // 16,
                        'Only bounded actual R_386_32/PC32 relocations')
                relocations[str(kind)] = relocations.get(str(kind), 0) + 1
        if section[2] & 4 and section[5]:
            require(section[1] == 1 and section[3] == 0
                    and re.fullmatch(r'[A-Za-z0-9_.$]+', label),
                    'Named indexed zero-based executable PROGBITS section')
            executable.append({'index': index, 'name': label, 'bytes': section[5]})
    require(executable, 'At least one real executable section')
    return sections, executable, undefined, relocations


def review_object(name, path, digest, expected_size, policy):
    require(path.resolve(strict=True) == path and not path.is_symlink(), 'Fixed physical native object')
    bind(path, digest)
    data = path.read_bytes()
    require(len(data) == expected_size, 'Whole qualified object size')
    sections, executable, undefined, relocations = object_sections(data)
    log = OUT / (name + '.disassembly.txt')
    argv = [str(TOOL), '-d', '-w', '-z', '--insn-width=16', str(path)]
    with log.open('xb') as stream:
        result = subprocess.run(argv, cwd=OUT, env=ENV, shell=False, close_fds=True,
                                stdin=subprocess.DEVNULL, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=60)
    state['commands'].append({'object': name, 'argv': argv, 'shell': False,
                              'returncode': result.returncode})
    require(result.returncode == 0 and log.stat().st_size < 8 * 1024 * 1024, 'Bounded successful disassembler output')
    allowed, allowed_registers, prefix_pairs, cs_nops = policy
    seen = {}
    mnemonics = {}
    registers = set()
    cursor = 0
    current = None
    section_size = 0
    for line in log.read_text().splitlines():
        heading = re.fullmatch(r'Disassembly of section (.+):', line)
        if heading:
            require(cursor < len(executable) and heading[1] == executable[cursor]['name'],
                    'Every actual executable section appears in exact ELF index order')
            current = str(executable[cursor]['index'])
            section_size = executable[cursor]['bytes']
            seen[current] = 0
            cursor += 1
            continue
        match = INSTRUCTION.match(line)
        if not match:
            continue
        address = int(match[1], 16)
        raw = bytes.fromhex(match[2])
        assembly = match[3]
        require(current is not None and address == seen[current]
                and 0 < len(raw) <= 15 and address + len(raw) <= section_size,
                'Exact contiguous instruction boundaries over the complete indexed section')
        start = sections[int(current)][4] + address
        require(raw == data[start:start + len(raw)], 'Decoded bytes equal actual whole-object bytes')
        prefixes, mnemonic = instruction_words(assembly)
        require(mnemonic in allowed and mnemonic != 'int',
                'Instruction outside the pre-existing accepted native ISA: ' + assembly)
        require(not prefixes or (mnemonic, tuple(prefixes)) in prefix_pairs,
                'Instruction prefix outside the pre-existing accepted combinations: ' + assembly)
        operands = assembly.split('#', 1)[0].split('<', 1)[0]
        actual_registers = set(re.findall(r'%[A-Za-z][A-Za-z0-9]*(?:\([0-9]+\))?', operands))
        legacy_prefixes = []
        for byte in raw:
            if byte not in (0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf0, 0xf2, 0xf3):
                break
            legacy_prefixes.append(byte)
        require(not set(legacy_prefixes) & {0x26, 0x36, 0x3e, 0x64, 0x65},
                'No hardware TLS or other segment override')
        if '%cs' in actual_registers or 0x2e in legacy_prefixes:
            require(raw in cs_nops and mnemonic in ('nop', 'nopl', 'nopw'),
                    'CS only in the exact independently accepted padding bytes')
            actual_registers.discard('%cs')
        require(actual_registers <= allowed_registers,
                'Register outside the pre-existing accepted native subset: ' + assembly)
        registers.update(actual_registers)
        seen[current] += len(raw)
        mnemonics[mnemonic] = mnemonics.get(mnemonic, 0) + 1
    require(cursor == len(executable)
            and seen == {str(item['index']): item['bytes'] for item in executable},
            'Every executable byte of every object section decoded exactly once')
    state['outputs'][str(log)] = sha(log)
    state['objects'][name] = {'path': str(path), 'sha256': digest, 'bytes': len(data),
                              'decoded_bytes': sum(seen.values()), 'section_bytes': executable,
                              'mnemonics': mnemonics, 'registers': sorted(registers),
                              'relocation_types': relocations, 'undefined_symbols': undefined,
                              'hardware_TLS_sections': 0, 'init_fini_preinit_array_bytes': 0,
                              'direct_native_syscall_sites': 0,
                              'complete_executable_section_coverage': True,
                              'disassembly_sha256': sha(log), 'target_executed': False}
    emit()


try:
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode
            and sys.flags.no_site and pathlib.Path(__file__).resolve() == Q / 'isa-review-v1.py',
            'Fixed isolated no-site ISA entry required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__)
            and guard['launcher_sha256'] == sha(Q / 'launch-isa-review-v1.py')
            and guard['landlock_applied'] and guard['landlock_abi'] >= 6
            and guard['network_disabled'] and guard['workspace_read_only']
            and guard['source_write_denied'] and not guard['directory_execute_permissions']
            and not guard['output_symlink_creation_allowed']
            and guard['writable_output'] == str(OUT)
            and guard['own_child_subreaper'] and guard['controller_starts_new_session'],
            'Exact inherited ISA guard required')
    require(all(os.stat('/proc/self/ns/' + name).st_ino == guard['namespace_ids'][name]
                for name in ('user', 'net'))
            and ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1,
            'Unchanged private namespaces and no_new_privs required')
    require(set(os.environ) <= {'PATH', 'LANG', 'TZ', 'LC_CTYPE'}
            and all(os.environ.get(key) == value for key, value in ENV.items()), 'Fixed minimum environment')
    require(pathlib.Path(sys.executable).resolve() == pathlib.Path('/usr/bin/python3.8')
            and guard['executable_files'] == {str(PYTHON.resolve(strict=True)): PYTHON_SHA,
                                              str(TOOL.resolve(strict=True)): TOOL_SHA},
            'Only the two fixed qualified executables')
    authorized = True
    bind(__file__, guard['child_sha256'])
    bind(Q / 'launch-isa-review-v1.py', guard['launcher_sha256'])
    bind(TOOL, TOOL_SHA, tool=True)
    bind(PYTHON, PYTHON_SHA, tool=True)
    policy = prior_policy()
    compiled = receipt(COMPILE, COMPILE_SHA)
    require(all(compiled[key] for key in ('complete', 'diagnostic_complete',
                'formal_CXX_ASM_pass', 'full_CXX_M_MD_source_qualification',
                'bound_inputs_unchanged', 'aliases_unchanged', 'outputs_unchanged',
                'old_cache_metadata_unchanged', 'original_format_object_reused_without_recompile'))
            and not compiled['unqualified_M_inputs']
            and set(compiled['objects']) == {'checks', 'positive-entry', 'negative-entry', 'start'},
            'Actual complete three-CXX/one-ASM qualification required')
    for path, digest in compiled['inputs'].items():
        bind(path, digest)
    for logical, actual in compiled['aliases'].items():
        require(str(pathlib.Path(logical).resolve(strict=True)) == actual
                and W in pathlib.Path(actual).parents, 'Qualified compile alias changed or escaped')
        state['aliases'][logical] = actual
    require(len([row for row in compiled['commands'] if row['name'].endswith('-M')]) == 3
            and len([row for row in compiled['commands'] if row['name'].endswith('-CXX')]) == 3
            and len([row for row in compiled['commands'] if row['name'].endswith('-ASM')]) == 1,
            'Exactly actual three CXX and one standalone ASM command')
    original = receipt(FORMAT / 'status.json',
                       '0b7e704222a57503715e3d09d979e714d80665db43ebc22ef40ffaab33e58d11')
    format_object = FORMAT / 'build' / FORMAT_RELATIVE
    require(all(original[key] for key in ('complete', 'clipboard_format_compile_pass',
                'complete_M_MD_source_qualification', 'bound_inputs_unchanged',
                'aliases_unchanged', 'outputs_unchanged'))
            and original['M_dependency_count'] == 933
            and original['outputs'].get(str(format_object)) == FORMAT_SHA
            and compiled['reused_format_object']['path'] == str(format_object)
            and compiled['reused_format_object']['sha256'] == FORMAT_SHA
            and not compiled['reused_format_object']['recompiled'],
            'Independently qualified whole original format object retained')
    units = {'checks': 'probe.checks.o', 'positive-entry': 'probe.entry.o',
             'negative-entry': 'negative_entry.entry.o', 'start': 'probe.start.o'}
    for name, filename in units.items():
        path = Q / 'compile-01/build/obj/tools/gtos_clipboard_format_probe' / filename
        item = compiled['objects'][name]
        require(item['path'] == str(path) and item['standalone_ASM'] == (name == 'start')
                and compiled['outputs'].get(str(path)) == item['sha256'], 'Exact qualified native fixture object')
        review_object(name, path, item['sha256'], item['bytes'], policy)
    review_object('original-format', format_object, FORMAT_SHA, format_object.stat().st_size, policy)
    require(len(state['objects']) == 5 and len(state['commands']) == 5
            and all(item['complete_executable_section_coverage'] for item in state['objects'].values()),
            'All five complete real object instruction streams reviewed')
    require(all(sha(path) == digest for path, digest in state['inputs'].items())
            and all(str(pathlib.Path(logical).resolve(strict=True)) == actual
                    for logical, actual in state['aliases'].items())
            and all(sha(path) == digest for path, digest in state['outputs'].items())
            and all(not path.is_symlink() for path in OUT.rglob('*')),
            'Final actual inputs, aliases, reviewed disassembly outputs unchanged')
    state.update(complete=True, ISA_review_pass=True, bound_inputs_unchanged=True,
                 aliases_unchanged=True, outputs_unchanged=True,
                 all_indexed_executable_bytes_decoded_once_and_matched_to_object=True,
                 IA32_i686_x87_SSE2_only=True, hardware_TLS_sections=0,
                 init_fini_preinit_array_bytes=0, direct_native_syscall_sites=0,
                 decoded_bytes=sum(item['decoded_bytes'] for item in state['objects'].values()),
                 scope='Whole five exact objects: actual checks, separate positive/negative entries, '
                       'aligned standalone startup and original complete ClipboardFormatType. Fixed prior '
                       'accepted scalar/i686/x87/SSE2 policy, exact CS padding, no software interrupts, '
                       'MMX/AVX/new optional ISA or hardware TLS. Full executable-section byte coverage and '
                       'source/object/alias/output terminal checks. No link, native semantic, system clipboard, '
                       'Content/browser/web/video/HTML5 acceptance.')
    code = 0
except BaseException as error:
    if authorized:
        state.update(failure=str(error), traceback=traceback.format_exc())
    traceback.print_exc()
finally:
    if authorized:
        state['returncode'] = code
        emit()
sys.exit(code)

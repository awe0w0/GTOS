#!/usr/bin/env python3
"""Compile actual clipboard fixture GN rows; reuse the whole original format object."""
import ctypes
import hashlib
import json
import os
import pathlib
import shlex
import struct
import subprocess
import sys
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
Q = W / 'artifacts/native-clipboard-format-20261018-a'
P = W / 'artifacts/native-content-frontier-20261018-a'
B = W / 'artifacts/native-pma-20261017-a'
G = Q / 'gn-probe-01'
GN_SHA = '4432e32ab77e8c01d1a5e8d919176efc5745d37960523ace5ca7461e336523f2'
PINS_SHA = '2cdcd9bfaa37bde63741ec2cd25994492ba1e3333ccd72082058104274d70749'
OUT = Q / 'compile-01'
BUILD = OUT / 'build'
SOURCE = OUT / 'source'
FORMAT = P / 'clipboard-format-compile-02'
FORMAT_RELATIVE = 'obj/ui/base/clipboard/libui_base_clipboard_types.clipboard_format_type_aura.o'
FORMAT_SHA = 'ae8553d59c9683267df4cf0dd9502906dd4e400cf2449649148cf9c0e9097185'
CC = W / 'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
NM = CC.parent / 'llvm-nm'
TOOLS = {
    CC: '656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7',
    NM: '629e43f674d383d8c54d43f3254f80895a380c05a4afcdec27bb03dbb72b74bc',
}
state = {'complete': False, 'diagnostic_complete': False,
         'formal_CXX_ASM_pass': False, 'native_clipboard_pass': False,
         'browser_pass': False, 'inputs': {}, 'aliases': {}, 'commands': [],
         'objects': {}, 'outputs': {}, 'unqualified_M_inputs': {}}
known = {}
parents = {}
cache_metadata = {}
authorized = False
exit_code = 1


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def resolve(path, cache):
    path = pathlib.Path(os.path.normpath(str(path)))
    key = str(path.parent)
    if key not in cache:
        cache[key] = path.parent.resolve(strict=True)
    actual = cache[key] / path.name
    if actual.is_symlink():
        actual = actual.resolve(strict=True)
    actual.stat()
    return actual


def bind(path, digest=None):
    actual = resolve(path, parents)
    key = str(actual)
    digest = known.get(key) if digest is None else digest
    require(W in actual.parents and actual.is_file() and isinstance(digest, str)
            and len(digest) == 64, 'Unqualified workspace input: ' + key)
    if key in state['inputs']:
        require(state['inputs'][key] == digest, 'Input identity conflict')
    else:
        require(sha(actual) == digest, 'Changed input: ' + key)
        state['inputs'][key] = digest
        if actual.suffix in ('.o', '.a') and OUT not in actual.parents:
            metadata = actual.stat()
            cache_metadata[key] = (metadata.st_size, metadata.st_mtime_ns)
    state['aliases'][str(path)] = key


def receipt(path, digest):
    bind(path, digest)
    record = json.loads(path.read_text())
    require('failure' not in record, 'Failed prerequisite')
    return record


def import_origins(record):
    for path, digest in record['inputs'].items():
        require(path not in known or known[path] == digest, 'Immutable origin conflict')
        known[path] = digest


def emit():
    (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')


def run(name, argv):
    argv = [str(value) for value in argv]
    executable = pathlib.Path(argv[0])
    require(executable in TOOLS and sha(executable) == TOOLS[executable],
            'Only fixed compiler and NM subprocesses')
    log = OUT / (name + '.log')
    with log.open('xb') as stream:
        result = subprocess.run(argv, cwd=BUILD, env=environment, shell=False,
                                close_fds=True, stdin=subprocess.DEVNULL,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=300)
    state['commands'].append({'name': name, 'argv': argv, 'returncode': result.returncode,
                              'log_sha256': sha(log)})
    state['outputs'][str(log)] = sha(log)
    emit()
    require(result.returncode == 0 and log.stat().st_size < 8 * 1024 * 1024,
            'Failed ' + name + '; inspect retained bounded log')
    print('PASS ' + name, flush=True)
    return log


def dependencies(path):
    text = path.read_text().replace(chr(92) + chr(10), ' ')
    require(text.count(':') == 1 and chr(92) not in text and '$' not in text,
            'Plain complete native dependency syntax')
    names = shlex.split(text.split(':', 1)[1])
    require(0 < len(names) < 10000, 'Bounded complete dependency inventory')
    result = {}
    for name in names:
        logical = BUILD / name
        actual = resolve(logical, parents)
        require(W in actual.parents and actual.is_file(), 'Dependency escaped workspace')
        result[str(logical)] = str(actual)
    return result


def elf32_object(path):
    data = path.read_bytes()
    require(52 <= len(data) < 32 * 1024 * 1024 and data[:7] == b'\x7fELF\x01\x01\x01'
            and struct.unpack_from('<HH', data, 16) == (1, 3), 'Real ELF32/i386 object')
    shoff = struct.unpack_from('<I', data, 32)[0]
    shsize, shcount = struct.unpack_from('<2H', data, 46)
    require(shsize == 40 and 0 < shcount < 65535 and shoff + shsize * shcount <= len(data),
            'Bounded actual object section table')
    for index in range(shcount):
        section = struct.unpack_from('<10I', data, shoff + shsize * index)
        require(not section[2] & 0x400 and (section[1] == 8 or
                section[4] + section[5] <= len(data)), 'No hardware TLS; bounded sections')
    return len(data)


def nm_symbols(path):
    symbols = {}
    for line in path.read_text().splitlines():
        words = line.split()
        require(2 <= len(words) <= 4 and len(words[1]) == 1, 'Plain bounded NM row')
        name, kind = words[:2]
        require(name not in symbols, 'Unique object NM symbol: ' + name)
        symbols[name] = kind
    return symbols


try:
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode
            and sys.flags.no_site, 'Fixed Python -I -B -S entry')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__) and
            guard['launcher_sha256'] == sha(Q / 'launch-compile-probe-v1.py') and
            guard['landlock_applied'] and guard['network_disabled'] and
            guard['source_write_denied'] and guard['output_symlink_creation_allowed'] and
            not guard['directory_execute_permissions'] and guard['writable_output'] == str(OUT),
            'Exact offline read-only-workspace guard')
    require(all(os.stat('/proc/self/ns/' + name).st_ino == guard['namespace_ids'][name]
                for name in ('user', 'net')) and
            ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1,
            'Namespace/no_new_privs mismatch')
    authorized = True
    require(len(GN_SHA) == 64 and all(character in '0123456789abcdef' for character in GN_SHA),
            'PENDING: root must review and bind complete gn-probe-01/status.json')
    graph = receipt(G / 'status.json', GN_SHA)
    require(all(graph[key] for key in ('complete', 'gn_generation_pass',
                'typed_GN_dependency_qualification', 'formal_clipboard_fixture_targets',
                'prior_owners_byte_identical', 'whole_toolchain_and_generation_edges_preserved',
                'bound_inputs_unchanged', 'aliases_unchanged', 'directory_identity_unchanged',
                'reviewed_outputs_unchanged')) and
            (graph['GN_targets'], graph['GN_files']) == (21501, 2518),
            'Exact full Content graph and independent positive/negative fixture targets')
    pins = receipt(Q / 'probe-source-pins-01.json', PINS_SHA)
    relative_root = 'tools/gtos_clipboard_format_probe/'
    require(pins['authored_fixture_only'] and not pins['native_runtime_qualified'] and
            pins['record_bytes'] == 256 and set(pins['files']) ==
            {relative_root + name for name in ('BUILD.gn', 'api.h', 'checks.cc', 'entry.cc', 'start.s')},
            'Only five independently reviewed authored fixture source files')
    original = receipt(FORMAT / 'status.json',
                       '0b7e704222a57503715e3d09d979e714d80665db43ebc22ef40ffaab33e58d11')
    require(all(original[key] for key in ('complete', 'clipboard_format_compile_pass',
                'complete_M_MD_source_qualification', 'bound_inputs_unchanged',
                'aliases_unchanged', 'outputs_unchanged')) and
            original['M_dependency_count'] == 933 and not original['unqualified_M_inputs'],
            'Original whole format object with complete independently qualified 933 M/MD inputs')
    classes = receipt(B / 'class-formal-02/status.json',
                      '5c0bd4beda58acb82a5446151112fe65a47179f78335993ccc574d7b10648d21')
    require(all(classes[key] for key in ('complete', 'full_CXX_M_MD_source_qualification',
                'bound_inputs_unchanged', 'aliases_unchanged')), 'Prior actual native fixture origins')
    root = receipt(B / 'class-link-03/status.json',
                   'f5218569cbfe1f22ab490508ec04f5ff53d07bc90a5f4b976b10801741564846')
    require(all(root[key] for key in ('complete', 'separate_class_link_pass',
                'bound_inputs_unchanged', 'aliases_unchanged', 'old_cache_metadata_unchanged')),
            'Accepted dependency pool origins only; no new link or guest claim')
    base_path = W / 'artifacts/native-base-closure-20261017-c/compile-01/status.json'
    base = receipt(base_path, '34e139a8ed534e4d3d8f6dce1370b3cc29a59593effd8327b1f58188224fafcf')
    require(all(base[key] for key in ('all_CXX_pass', 'complete_MD_source_qualification',
                'bound_inputs_unchanged', 'aliases_unchanged')), 'Prior original base/header origins')
    for record in (graph, original, classes, root, base):
        import_origins(record)
    # Reusing the original object requires its actual prior source/alias closure,
    # rather than only trusting the object hash or inferring origins from M.
    for path, digest in original['inputs'].items():
        bind(path, digest)
    for logical, actual in original['aliases'].items():
        require(str(resolve(logical, parents)) == actual, 'Original format alias changed')
        state['aliases'][logical] = actual
    for relative, item in pins['files'].items():
        bind(item['path'], item['sha256'])
        bind(G / 'source' / relative, item['sha256'])
        actual = str(resolve(item['path'], parents))
        require(actual not in known or known[actual] == item['sha256'], 'Fixture origin conflict')
        known[actual] = item['sha256']
    format_object = FORMAT / 'build' / FORMAT_RELATIVE
    require(original['outputs'].get(str(format_object)) == FORMAT_SHA, 'Exact original format output')
    bind(format_object, FORMAT_SHA)
    elf32_object(format_object)
    state['reused_format_object'] = {'path': str(format_object), 'sha256': FORMAT_SHA,
                                      'recompiled': False, 'M_dependencies': 933}
    for path, digest in TOOLS.items():
        bind(path, digest)
    BUILD.mkdir()
    SOURCE.symlink_to(G / 'source', target_is_directory=True)
    state['aliases'][str(SOURCE)] = str((G / 'source').resolve(strict=True))
    gen = resolve(FORMAT / 'build/gen', parents)
    require(W in gen.parents and gen.is_dir(), 'Qualified existing generation view')
    (BUILD / 'gen').symlink_to(gen, target_is_directory=True)
    state['aliases'][str(BUILD / 'gen')] = str(gen)
    for relative in ('build/config/warning_suppression.txt',
                     'build/config/raw_ptr_plugin_config.yaml', 'build/config/unsafe_buffers_paths.txt'):
        bind(SOURCE / relative)
    for name in ('home', 'tmp', 'crash'):
        (OUT / name).mkdir()
    environment = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC',
                   'HOME': str(OUT / 'home'), 'TMPDIR': str(OUT / 'tmp')}
    cxx = [str(CC), '-x', 'c++', '--target=i686-unknown-none-elf', '-ffreestanding',
           '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fno-unwind-tables',
           '-fno-asynchronous-unwind-tables', '-mno-mmx', '-msse2', '-mfpmath=sse',
           '-mno-sse3', '-mno-ssse3', '-mno-sse4', '-mno-avx', '-femulated-tls',
           '-fstack-usage', '-nostdinc', '-nostdinc++', '-DGTOS_NATIVE_SDK=1',
           '-isystem', '../source/tools/gtos_native_sdk/sdk', '-isystem',
           '../source/third_party/libc++/src/include', '-isystem',
           '../source/tools/gtos_native_sdk/c-sdk', '-isystem',
           '../../../../toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include',
           '-fno-exceptions', '-fno-rtti']
    assembly = [str(CC), '--target=i686-unknown-none-elf', '-nostdinc']
    tool = G / 'build/toolchain.ninja'
    bind(tool, graph['native_toolchain_sha256'])
    lines = tool.read_text().splitlines()
    require(lines.count('rule cxx') == lines.count('rule asm') == 1 and
            lines[lines.index('rule cxx') + 1] == '  command = ' + ' '.join(cxx) +
            ' ${defines} ${include_dirs} ${cflags} ${cflags_cc} -MD -MF ${out}.d -c ${in} -o ${out}' and
            lines[lines.index('rule asm') + 1] == '  command = ' + ' '.join(assembly) +
            ' ${defines} ${include_dirs} ${asmflags} -MD -MF ${out}.d -c ${in} -o ${out}',
            'Exact typed native CXX/ASM tool rules; no command/log dispatch')
    units = [('checks', 'probe', 'checks.cc', False),
             ('positive-entry', 'probe', 'entry.cc', False),
             ('negative-entry', 'negative_entry', 'entry.cc', False),
             ('start', 'probe', 'start.s', True)]
    prepared = []
    for name, stem, filename, is_asm in units:
        state['stage'] = name + '_actual_GN_and_dependency_preflight'
        emit()
        owner = 'obj/' + relative_root + stem + '.ninja'
        owner_path = G / 'build' / owner
        meta = graph['owners'][owner]
        bind(owner_path, meta['sha256'])
        lines = owner_path.read_text().splitlines()
        fields = meta['asm_fields' if is_asm else 'fields']
        flags = []
        for key in (('defines', 'include_dirs', 'asmflags') if is_asm else
                    ('defines', 'include_dirs', 'cflags', 'cflags_cc')):
            values = [line[len(key) + 3:] for line in lines if line.startswith(key + ' = ')]
            require(len(values) == 1 and values[0] == fields[key], 'Exact actual owner field')
            flags.extend(shlex.split(values[0]))
        output = 'obj/' + relative_root + stem + '.' + pathlib.PurePosixPath(filename).stem + '.o'
        source = '../source/' + relative_root + filename
        marker = ': asm ' if is_asm else ': cxx '
        members = [line for line in lines if line.startswith('build ' + output + marker + source + ' ')]
        require(len(members) == 1 and members[0] in
                meta['new_ASM_members' if is_asm else 'new_CXX_members'], 'Exact unique actual fixture member')
        require(not any(value.startswith(('@', '--sysroot', '--config=', '-fplugin=',
                    '-fpass-plugin=', '-ivfsoverlay', '-D__linux__', '-D__unix__')) for value in flags)
                and '-load' not in flags and '-DUSE_AURA' not in flags,
                'No host/plugin/path substitution or inherited Aura identity')
        if not is_asm:
            require(all(value in flags for value in ('-D__GTOS__=1', '-DNDEBUG',
                    '-DDCHECK_ALWAYS_ON=1', '-O2', '-std=c++23')) and
                    (('-DGTOS_CLIPBOARD_MODE=1' in flags) == (stem == 'negative_entry')),
                    'Real native hardening and only explicit negative-entry selection')
            crash = '-fcrash-diagnostics-dir=../source/out/clang-crashreports'
            require(flags.count(crash) == 1, 'Only crash output may move')
            flags[flags.index(crash)] = '-fcrash-diagnostics-dir=' + str(OUT / 'crash')
        bind(SOURCE / relative_root / filename)
        before = None
        if is_asm:
            text = (SOURCE / relative_root / filename).read_text()
            require(not any(token in text for token in ('.include', '.incbin', '#include')),
                    'Standalone aligned ASM without hidden includes')
            before = {str(BUILD / source): str(resolve(BUILD / source, parents))}
        else:
            pre = OUT / (name + '.pre.d')
            run(name + '-M', cxx + flags + ['-M', '-MT', output, '-MF', pre, source])
            state['outputs'][str(pre)] = sha(pre)
            before = dependencies(pre)
            for logical, actual in before.items():
                if actual in known:
                    bind(logical, known[actual])
                else:
                    state['unqualified_M_inputs'][actual] = sha(actual)
        prepared.append((name, owner, output, source, is_asm, flags, before, members[0]))
        emit()
    require(not state['unqualified_M_inputs'],
            'New full-M source requires independent origin before any CXX/ASM execution')
    for name, owner, output, source, is_asm, flags, before, member in prepared:
        state['stage'] = name + '_actual_GN_compile'
        emit()
        obj = BUILD / output
        obj.parent.mkdir(parents=True, exist_ok=True)
        dep = pathlib.Path(str(obj) + '.d')
        compile_log = run(name + ('-ASM' if is_asm else '-CXX'),
                          (assembly if is_asm else cxx) + flags +
                          ['-MD', '-MF', output + '.d', '-c', source, '-o', output])
        if is_asm:
            # The actual GN rule keeps -MD/-MF. Clang deliberately ignores
            # them for lowercase .s; no dependency file is invented here.
            require(not dep.exists() and not dep.is_symlink(),
                    'Standalone lowercase .s must not claim an emitted MD file')
            after = {str(BUILD / source): str(resolve(BUILD / source, parents))}
            require(after == before and all(sha(actual) == state['inputs'][actual]
                    for actual in after.values()), 'Pinned single ASM source and hash unchanged')
            ignored = ['-nostdinc'] + ['-D ' + value[2:] for value in flags
                                      if value.startswith('-D')]
            ignored += ['-MD', '-MF ' + output + '.d']
            warnings = ["clang: warning: argument unused during compilation: '" + value +
                        "' [-Wunused-command-line-argument]" for value in ignored]
            require(compile_log.read_text().splitlines() == warnings,
                    'Only exact known lowercase-ASM unused defines/nostdinc/MD/MF diagnostics')
        else:
            after = dependencies(dep)
            require(after == before, 'Three actual CXX units retain complete full M=MD')
        for logical, actual in after.items():
            bind(logical, known[actual])
        size = elf32_object(obj)
        undefined_log = run(name + '-undefined', [NM, '--undefined-only', '--format=posix', obj])
        defined_log = run(name + '-defined', [NM, '--defined-only', '--format=posix', obj])
        undefined = nm_symbols(undefined_log)
        defined = nm_symbols(defined_log)
        required = {'checks': {'NativeClipboardChecks'},
                    'positive-entry': {'NativeClipboardEntry', 'native_clipboard_record',
                              'native_v8_integration_stage', 'native_v8_integration_result'},
                    'negative-entry': {'NativeClipboardEntry', 'native_clipboard_record',
                                       'native_v8_integration_stage', 'native_v8_integration_result'},
                    'start': {'NativeClipboardStart'}}[name]
        require(required <= set(defined), 'Actual required scalar fixture ABI symbols')
        if name == 'start':
            require(set(undefined) == {'NativeClipboardEntry'}, 'Aligned start calls real CXX entry')
        if name in ('positive-entry', 'negative-entry'):
            require({'NativeClipboardChecks', 'exit', 'gtos_native_environment_initialize',
                     'gtos_native_heap_enable_large_allocations'} <= set(undefined),
                    'Real native initialization/check/exit providers remain external')
        state['outputs'][str(obj)] = sha(obj)
        if not is_asm:
            state['outputs'][str(dep)] = sha(dep)
        state['objects'][name] = {'path': str(obj), 'sha256': sha(obj), 'bytes': size,
                                  'MD_sha256': None if is_asm else sha(dep), 'dependencies': after,
                                  'actual_GN_owner': owner, 'actual_GN_row': member,
                                  'undefined_symbols': undefined, 'defined_symbols': defined,
                                  'diagnostic_only': True, 'standalone_ASM': is_asm,
                                  'depfile_generated': not is_asm, 'MD_evidence_fabricated': False,
                                  'dependency_evidence': 'Pinned standalone source; exact GN row/ASM log; no include/incbin'
                                      if is_asm else 'Complete independently qualified CXX M equals actual MD'}
        if is_asm:
            actual = next(iter(after.values()))
            state['objects'][name].update(source=actual, source_sha256=state['inputs'][actual],
                                          raw_standalone_ASM_one_explicit_source=True,
                                          exact_ignored_MD_MF_diagnostics=True)
        emit()
    state['stage'] = 'terminal_effective_input_alias_cache_output_checks'
    emit()
    fresh = {}
    require(all(sha(path) == digest for path, digest in state['inputs'].items()) and
            all(str(resolve(logical, fresh)) == actual for logical, actual in state['aliases'].items()) and
            all(sha(path) == digest for path, digest in state['outputs'].items()) and
            not pathlib.Path(state['objects']['start']['path'] + '.d').exists() and
            not pathlib.Path(state['objects']['start']['path'] + '.d').is_symlink() and
            all((pathlib.Path(path).stat().st_size, pathlib.Path(path).stat().st_mtime_ns) == metadata
                for path, metadata in cache_metadata.items()), 'Input/alias/cache/output changed')
    require(set(state['objects']) == {'checks', 'positive-entry', 'negative-entry', 'start'} and
            len([row for row in state['commands'] if row['name'].endswith('-M')]) == 3 and
            len([row for row in state['commands'] if row['name'].endswith('-CXX')]) == 3 and
            len([row for row in state['commands'] if row['name'].endswith('-ASM')]) == 1,
            'Exactly three real CXX, one real ASM and three complete preflights')
    state.update(complete=True, diagnostic_complete=True, formal_CXX_ASM_pass=True,
                 full_CXX_M_MD_source_qualification=True, bound_inputs_unchanged=True,
                 aliases_unchanged=True, old_cache_metadata_unchanged=True, outputs_unchanged=True,
                 original_format_object_reused_without_recompile=True, stage='completed',
                 scope='Actual independent clipboard fixture GN rows: checks/positive entry/negative entry CXX '
                       'and aligned standalone ASM. Original whole ClipboardFormatType object is reused byte for byte. '
                       'All three CXX full-M origins authenticated before compilation and exact M=MD checked; '
                       'lowercase standalone ASM has one pinned source, no includes/incbin and no emitted/fabricated MD. '
                       'Exact GN row, ignored MD/MF diagnostics, source hash and native ELF32/scalar ABI checked; '
                       'only crash-output path relocated. No legacy class object equality requirement, link, ISA/native '
                       'semantic acceptance, desktop clipboard service or Content/browser/web/video/HTML5 claim.')
    emit()
    print('PASS actual native clipboard fixture CXX/ASM and original format reuse', flush=True)
    exit_code = 0
except BaseException as error:
    if authorized:
        state.update(failure=str(error), traceback=traceback.format_exc())
        emit()
    traceback.print_exc()
sys.exit(exit_code)

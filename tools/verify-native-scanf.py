#!/usr/bin/env python3
"""Replay a qualified GN sscanf object in its recorded build context; no full-V8 claim."""
import argparse
import ctypes
import datetime
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
import traceback


parser = argparse.ArgumentParser(description='Replay the actual qualified GN scanner object in an offline, confined GTOS guest probe')
parser.add_argument('output', type=pathlib.Path)
for option in ('workspace', 'source-root', 'module-root', 'integration-build', 'support-build',
               'scanner-compile', 'tool-state', 'fixture-root', 'runtime-root', 'integer-builtins',
               'input-manifest', 'clang', 'clang-resource-dir', 'cxx', 'qemu', 'firmware',
               'assembler', 'linker', 'nm', 'objcopy', 'readelf', 'objdump', 'grub-mkrescue'):
    parser.add_argument('--' + option, type=pathlib.Path, required=True)
parser.add_argument('--input-manifest-sha256', required=True)
parser.add_argument('--tool-state-sha256', required=True)
args = parser.parse_args()
WORKSPACE = args.workspace.resolve(strict=True)
REPO = args.source_root.resolve(strict=True)
PACKET = pathlib.Path(__file__).resolve().parent
SOURCE = args.fixture_root.resolve(strict=True)
OUT = args.output.resolve()
PREPARE = args.integration_build.resolve(strict=True)
SUPPORT = args.support_build.resolve(strict=True)
MODULE = args.module_root.resolve(strict=True)
COMPILE = args.scanner_compile.resolve(strict=True)
SCANNER = COMPILE / 'sscanf.o'
PRIOR = args.tool_state.resolve(strict=True)
INPUT_MANIFEST = args.input_manifest.resolve(strict=True)
RUNTIME = args.runtime_root.resolve(strict=True)
FIRMWARE = args.firmware.resolve(strict=True)
SCALAR = args.integer_builtins.resolve(strict=True)
PYTHON = pathlib.Path('/usr/bin/python3')
CLANG = args.clang.resolve(strict=True)
RESOURCE = args.clang_resource_dir.resolve(strict=True)
CXX = args.cxx.resolve(strict=True)
TOOLS = {
    'as': args.assembler.resolve(strict=True),
    'ld': args.linker.resolve(strict=True),
    'nm': args.nm.resolve(strict=True),
    'objcopy': args.objcopy.resolve(strict=True),
    'readelf': args.readelf.resolve(strict=True),
    'objdump': args.objdump.resolve(strict=True),
    'grub-mkrescue': args.grub_mkrescue.resolve(strict=True),
    'qemu-system-i386': args.qemu.resolve(strict=True),
}
PYTHON_SHA = '298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
ROOTS = {
    'workspace': WORKSPACE, 'repo': REPO, 'fixture': SOURCE, 'module': MODULE,
    'prepare': PREPARE, 'support': SUPPORT, 'scanner': COMPILE,
    'runtime': RUNTIME, 'system': pathlib.Path('/'),
}
SCANNER_SHA = None
expected_inputs = {}
SUPPORT_NAMES = (
    'obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.heap.o',
    'obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.operators.o',
    'obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.emutls.o',
    'obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.abort.o',
    'obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.thread-exit.o',
    'obj/tools/gtos_isolate_diagnostic/runtime/native_runtime.thread-core.o',
    'obj/third_party/llvm-libc/src/src/errno/native_c_clock.libc_errno.o',
    'obj/third_party/llvm-libc/src/src/string/native_string_libc.memcpy.o',
    'obj/third_party/llvm-libc/src/src/string/native_string_libc.memset.o',
)
RECORD_FIELDS = (
    'version', 'mode', 'stage', 'checks', 'error', 'formattedBytes',
    'textBytes', 'expectedBytes', 'progress', 'region', 'base', 'length',
    'reserved0', 'reserved1', 'reserved2', 'reserved3',
)
state = {
    'scope': 'Qualified GN scanner object replay in GTOS IA32 CPL3: probe consumer and private kernel rebuilt; complete GN target is not recompiled or qualified by this tool',
    'complete_target_recompiled': False,
    'reused_qualified_scanner_object': False,
    'all_required_checks_pass': False,
    'native_scanf_guest_pass': False,
    'actual_gn_object_executed': False,
    'actual_gn_object_sha256': SCANNER_SHA,
    'executed_scanner_object_sha256': None,
    'source_before_after_identical': False,
    'network_disabled': False,
    'file_system_isolated': False,
    'native_multithread_waits_pass': False,
    'native_isolate_pass': False,
    'javascript_guest_pass': False,
    'browser_guest_pass': False,
    'video_guest_pass': False,
    'html5_guest_pass': False,
    'production_image_changed': False,
    'capacity_changed': False,
    'commands': [], 'inputs': {}, 'guests': [],
}
environment = {}
allowed_programs = set()
output_authorized = False


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def emit(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def output_path(path):
    path = pathlib.Path(path)
    require(path.is_absolute() and OUT in path.parents, 'Output outside reviewed fresh directory')
    require(path.resolve() == path, 'Output symlink or noncanonical path')
    return path


def relative_input(root, relative):
    require(isinstance(relative, str) and relative and '\\' not in relative,
            'Manifest paths must be relative POSIX file names')
    name = pathlib.PurePosixPath(relative)
    require(not name.is_absolute() and not any(part in ('.', '..') for part in name.parts),
            'Manifest traversal or absolute input forbidden')
    path = (root / relative).resolve(strict=True)
    require(path.is_file() and (WORKSPACE in path.parents or pathlib.Path('/usr') in path.parents),
            'Manifest input outside workspace or reviewed system tools')
    if root == pathlib.Path('/'):
        require(relative.startswith('usr/'), 'Only /usr inputs are allowed in the system manifest role')
    return path


def register_input(path, digest):
    path = pathlib.Path(path).resolve(strict=True)
    require(isinstance(digest, str) and re.fullmatch('[0-9a-f]{64}', digest), 'Invalid input SHA256')
    previous = expected_inputs.get(str(path))
    require(previous is None or previous == digest, 'Conflicting manifest or source-lock input')
    expected_inputs[str(path)] = digest


def bind(path, expected=None):
    path = pathlib.Path(path).resolve(strict=True)
    require(WORKSPACE in path.parents or pathlib.Path('/usr') in path.parents,
            'Input outside workspace or fixed system tool directory: ' + str(path))
    if expected is None:
        expected = expected_inputs.get(str(path))
    require(isinstance(expected, str) and re.fullmatch('[0-9a-f]{64}', expected),
            'Consumed input absent from the supplied manifest or qualified evidence: ' + str(path))
    actual = sha(path)
    require(actual == expected, 'Changed reviewed input: ' + str(path))
    previous = state['inputs'].get(str(path))
    require(previous is None or previous == actual, 'Input changed during execution: ' + str(path))
    if previous is None:
        state['inputs'][str(path)] = actual
        try:
            relative = path.relative_to(WORKSPACE)
        except ValueError:
            relative = pathlib.Path('system') / path.relative_to('/')
        destination = output_path(OUT / 'source-snapshot' / relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
    return actual


def dependencies(path, cwd):
    data = path.read_text().replace('\\\n', ' ')
    require(':' in data, 'Invalid dependency file')
    words = data.split(':', 1)[1].split()
    require(words and all('\\' not in word for word in words), 'Unsupported dependency quoting')
    return [(cwd / word).resolve(strict=True) for word in words]


def run(argv, log, cwd=REPO, expected=0, timeout=120):
    argv = [str(item) for item in argv]
    program = pathlib.Path(argv[0])
    require(program.is_absolute() and program in allowed_programs, 'Unreviewed executable')
    require(cwd in (REPO, PREPARE / 'build'), 'Unreviewed working directory')
    require(all('$(' not in item and '`' not in item and item not in ('&&', ';', '|') for item in argv),
            'Shell syntax forbidden')
    require(sha(program) == state['inputs'][str(program.resolve())], 'Executable changed')
    if program == PYTHON:
        require(argv[1:3] == ['-I', '-B'] and '-c' not in argv and '--command' not in argv,
                'Fixed isolated Python file invocation required')
    log = output_path(log)
    began = time.monotonic()
    with log.open('wb') as stream:
        try:
            code = subprocess.run(argv, cwd=cwd, env=environment, shell=False, close_fds=True,
                                  stdout=stream, stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = 124
    state['commands'].append({'argv': argv, 'cwd': str(cwd), 'exit_code': code,
                              'seconds': time.monotonic() - began, 'log_sha256': sha(log)})
    emit(OUT / 'commands.json', state['commands'])
    require(code == expected, str(log) + '\n' + log.read_text(errors='replace')[-3500:])
    return log.read_text(errors='replace')


def numbers(line):
    return {key: int(value, 16) for key, value in re.findall(r'(\w+)=([0-9A-F]{8})(?=\s|$)', line)}


def observe(text, frame_cost):
    spurious = text.count('UNHANDLED INTERRUPT 0x00000027')
    clean = text.replace('UNHANDLED INTERRUPT 0x00000027', '')
    require(not any(word in clean for word in ('UNHANDLED INTERRUPT', 'PANIC', 'FAILED', 'FAIL')),
            'Guest failure or unexpected interrupt')
    require(clean.count('NATIVE SCAN SMOKE BOOT\n') == 1 and
            clean.count('NATIVE SCAN SMOKE PASS\n') == 1, 'Exactly one boot and final pass required')
    lines = clean.splitlines()
    cases = [line for line in lines if line.startswith('SCAN CASE ')]
    reaps = [numbers(line) for line in lines if line.startswith('SCAN REAP ')]
    finals = [numbers(line) for line in lines if line.startswith('SCAN FINAL ')]
    faults = [numbers(line) for line in lines if line.startswith('NATIVE USER FAULT ')]
    require(len(cases) == len(reaps) == 7 and len(finals) == 1 and len(faults) == 3,
            'All seven victims, three pointer faults and final peer Reap required')
    records = []
    for mode, line in zip((0, 1, 2, 3, 4, 5, 0), cases):
        require(line.count(' words= ') == 1, 'Missing fixed-size native record')
        head = numbers(line.split(' words= ', 1)[0])
        require(set(head) == {'id', 'mode', 'cs', 'cr3', 'exit', 'vector', 'cost', 'written', 'fault', 'fault_error'},
                'Unexpected native case header fields')
        raw = line.split(' words= ', 1)[1].split()
        require(len(raw) == 16 and all(re.fullmatch('[0-9A-F]{8}', word) for word in raw),
                'Exactly sixteen complete record words required')
        words = [int(word, 16) for word in raw]
        record = dict(zip(RECORD_FIELDS, words))
        require(words[:2] == [1, mode] and record['error'] == 0 and record['checks'] >= 5 and
                record['region'] and words[10:12] == [0x80000000, 4096] and words[13:] == [0, 0, 0],
                'Invalid version, mode, checks, retained region or reserved record fields')
        require(head['id'] and head['mode'] == mode and head['cs'] == 0x23 and
                head['cr3'] and not (head['cr3'] & 4095) and head['written'] == 0,
                'Actual CPL3, private page directory and no diagnostic output required')
        require(head['cost'] == frame_cost + (16 if mode == 5 else 0), 'Unexpected retained frame count')
        require(record['formattedBytes'] == record['expectedBytes'] == 0, 'Unexpected byte counters')
        if mode in (0, 1):
            require(record['stage'] == 2 and record['checks'] == (52 if mode == 0 else 9) and
                    record['textBytes'] == (41 if mode == 0 else 1) and record['progress'] == 0 and
                    words[12:] == [0, 0, 0, 0] and
                    head['exit'] == head['vector'] == head['fault'] == head['fault_error'] == 0,
                    'Scanner cases, page-tail safety or OOM recovery did not finish')
        elif mode == 5:
            progress = record['progress']
            require(record['stage'] == 1 and progress >= 3 and
                    6 + progress <= record['checks'] <= 7 + progress and record['textBytes'] == 0 and
                    0x80001000 <= record['reserved0'] <= 0x803f0000 and not (record['reserved0'] & 4095) and
                    head['exit'] == 73 and head['vector'] == head['fault'] == head['fault_error'] == 0,
                    'Scanning-loop cancellation with retained heap arena failed')
        else:
            require(record['stage'] == 1 and record['checks'] == 5 and record['textBytes'] == 0 and
                    record['progress'] == 0 and words[12:] == [0, 0, 0, 0] and
                    head['exit'] == 0x8000000e and head['vector'] == 14 and head['fault'] == 0xbfffcff0 and
                    head['fault_error'] == (6 if mode == 2 else 4),
                    'Output write fault or input/format read fault was not precisely contained')
        records.append({'header': head, 'words': words, 'record': record})
    require(len({row['header']['id'] for row in records}) == 7, 'Fresh victim IDs required')
    require([row['id'] for row in faults] == [row['header']['id'] for row in records[2:5]] and
            all(row['vector'] == 14 for row in faults), 'Fault notifications do not match the victims')
    require(all(set(row) == {'free', 'expected'} and row['free'] == row['expected'] == reaps[0]['expected']
                for row in reaps), 'Exact per-victim free-frame baseline not restored')
    final = finals[0]
    require(set(final) == {'free', 'expected'} and final['free'] == final['expected'] == reaps[0]['free'] + 7,
            'Exact final integer-peer frame reclamation failed')
    order = [line.split(' ', 2)[:2] for line in lines if line.startswith(('SCAN CASE ', 'SCAN REAP ', 'SCAN FINAL '))]
    require(order == [['SCAN', kind] for kind in ('CASE', 'REAP') * 7 + ('FINAL',)],
            'Victim/reap evidence out of order')
    return {
        'cases': records, 'reaps': reaps, 'final': final, 'faults': faults,
        'exact_reap_checks': 8, 'actual_victim_cases': 7, 'normal_scanner_cases': 82,
        'page_tail_read_checks_pass': True, 'oom_recovery_pass': True,
        'output_write_and_input_format_read_faults_pass': True,
        'scanning_loop_cancellation_and_heap_arena_reap_pass': True,
        'scanner_internal_buffer_live_at_cancellation_proven': False,
        'private_cr3_retained_marker_peer_and_fp_kernel_assertions_reached': True,
        'ignored_spurious_irq7_count': spurious,
    }


try:
    require(pathlib.Path(__file__).name == 'verify-native-scanf.py' and WORKSPACE != pathlib.Path('/'),
            'Named reviewed child and a bounded workspace required')
    require(all(root.is_dir() and WORKSPACE in root.parents for name, root in ROOTS.items()
                if name not in ('workspace', 'system')), 'All input roles must stay inside the supplied workspace')
    require(WORKSPACE in PACKET.parents and WORKSPACE in OUT.parents and
            WORKSPACE in PRIOR.parents and WORKSPACE in INPUT_MANIFEST.parents,
            'Tools, output and controlling evidence must stay in the supplied workspace')
    require(TOOLS['qemu-system-i386'] == RUNTIME / 'root/usr/bin/qemu-system-i386' and
            FIRMWARE == RUNTIME / 'root/usr/share/qemu' and
            WORKSPACE / 'toolchains' in RUNTIME.parents, 'Fixed runtime tool/firmware layout required')
    require(all(re.fullmatch('[0-9a-f]{64}', value) for value in
                (args.input_manifest_sha256, args.tool_state_sha256)), 'Explicit reviewed evidence SHA256 required')
    require(sys.flags.isolated and sys.flags.dont_write_bytecode and
            pathlib.Path(sys.executable).resolve() == pathlib.Path('/usr/bin/python3.8') and sha(PYTHON) == PYTHON_SHA,
            'Fixed qualified /usr/bin/python3 -I -B required')
    require(set(os.environ) <= {'PATH', 'LANG', 'TZ', 'LC_CTYPE'} and
            os.environ.get('PATH') == '/usr/bin:/bin' and os.environ.get('LANG') == 'C.UTF-8' and
            os.environ.get('TZ') == 'UTC', 'Inherited environment must be discarded')
    require(OUT.is_dir() and OUT.resolve() == OUT and not OUT.is_symlink() and
            {path.name for path in OUT.iterdir()} == {'guard.json', 'launcher.log'}, 'Fresh guarded output required')
    guard = json.loads((OUT / 'guard.json').read_text())
    namespace_ids = {name: os.stat('/proc/self/ns/' + name).st_ino for name in ('net', 'user')}
    require(namespace_ids == guard['namespace_ids'] and all(namespace_ids.values()),
            'Child must inherit the exact isolated network and user namespaces')
    uid_map = guard['outer_user_namespace_uid_map']
    require(len(uid_map) == 3 and uid_map[0] == '0' and uid_map[2] == '1', 'Outer isolated user namespace required')
    host_libc = ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6', use_errno=True)
    host_libc.prctl.restype = ctypes.c_int
    require(os.getuid() == 0 and host_libc.prctl(ctypes.c_int(39), ctypes.c_ulong(0), ctypes.c_ulong(0),
            ctypes.c_ulong(0), ctypes.c_ulong(0)) == 1, 'Actual namespace UID or no_new_privs inheritance missing')
    require(guard['landlock_applied'] and guard['landlock_abi'] >= 6 and guard['writable_output'] == str(OUT) and
            guard['workspace_read_only'] and guard['network_disabled'] and
            guard['child_sha256'] == sha(__file__) and
            guard['launcher_sha256'] == sha(PACKET / 'launch-native-scanf.py') and
            guard['source_write_denied'] and guard['rule_path_handles_retained'] and
            guard['input_manifest_sha256'] == args.input_manifest_sha256 and
            guard['tool_state_sha256'] == args.tool_state_sha256 and
            guard['configuration_sha256'] == hashlib.sha256(json.dumps(sys.argv[1:], separators=(',', ':')).encode()).hexdigest() and
            guard['python_path'] == str(PYTHON) and guard['python_sha256'] == PYTHON_SHA,
            'Hash-bound launcher and Landlock restriction required')
    output_authorized = True
    state.update(network_disabled=True, file_system_isolated=True, inherited_environment_discarded=True,
                 execution_guards='Fixed tools and statically reviewed argv; outer unshare user/net; Landlock ABI >= 6; only fresh output writable')
    (OUT / 'home').mkdir()
    (OUT / 'tmp').mkdir()
    environment = {
        'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC',
        'HOME': str(OUT / 'home'), 'TMPDIR': str(OUT / 'tmp'),
        'LD_LIBRARY_PATH': str(RUNTIME / 'root/lib/x86_64-linux-gnu') + ':' + str(RUNTIME / 'root/usr/lib/x86_64-linux-gnu'),
        'QEMU_MODULE_DIR': str(RUNTIME / 'root/usr/lib/x86_64-linux-gnu/qemu'),
    }
    bind(__file__, guard['child_sha256'])
    bind(PACKET / 'launch-native-scanf.py', guard['launcher_sha256'])
    bind(INPUT_MANIFEST, args.input_manifest_sha256)
    manifest = json.loads(INPUT_MANIFEST.read_text())
    require(manifest['format'] == 'gtos-native-scanf-guest-inputs-v1' and
            set(manifest['files']) == set(ROOTS), 'Reviewed role-based input manifest required')
    for role, records in manifest['files'].items():
        require(isinstance(records, dict), 'Invalid manifest role records')
        for relative, digest in records.items():
            path = relative_input(ROOTS[role], relative)
            register_input(path, digest)
            bind(path, digest)
    require(set(manifest['files']['fixture']) == {'main.cc', 'kernel.cpp', 'record.h',
            'sscanf-cases-v1.h', 'linker.ld', 'counts.h'}, 'Exactly the reviewed six scanner fixtures required')
    SCANNER_SHA = manifest['scanner_object_sha256']
    bind(SCANNER, SCANNER_SHA)
    bind(SCALAR, manifest['integer_builtins_sha256'])
    bind(MODULE / 'source-lock.json', manifest['source_lock_sha256'])
    bind(MODULE / 'payload-lock.json', manifest['payload_lock_sha256'])
    source_lock = json.loads((MODULE / 'source-lock.json').read_text())
    require(source_lock['schema_version'] == 1 and
            source_lock['payload_lock_sha256'] == manifest['payload_lock_sha256'] and
            source_lock['tool_sha256']['clang'] == sha(CLANG) and
            source_lock['tool_sha256']['integer_builtins'] == sha(SCALAR),
            'Source lock must bind this payload, compiler and integer runtime')
    for relative, digest in source_lock['clang_resource_files'].items():
        register_input(relative_input(RESOURCE, relative), digest)
    bind(PRIOR, args.tool_state_sha256)
    prior = json.loads(PRIOR.read_text())
    require(prior['all_required_checks_pass'] and prior['network_disabled'], 'Qualified offline tool/object evidence required')
    for program in (PYTHON, CLANG, CXX, *TOOLS.values()):
        digest = PYTHON_SHA if program == PYTHON else prior['inputs'].get(str(program.resolve()))
        require(digest is not None, 'Tool not present in qualified replay: ' + str(program))
        bind(program, digest)
        allowed_programs.add(program)
    # Bind only already-qualified library/firmware inputs from this explicit replay.
    for name, digest in prior['inputs'].items():
        path = pathlib.Path(name)
        if '.so' in path.name or str(path).startswith(str(RUNTIME / 'root/usr/share/')):
            require(WORKSPACE in path.parents or pathlib.Path('/usr/lib/x86_64-linux-gnu') in path.parents,
                    'Unqualified external runtime dependency')
            bind(path, digest)
    prepare = json.loads((PREPARE / 'status.json').read_text())
    require(prepare['gn_pass'] and prepare['bound_inputs_unchanged'] and not prepare.get('live_command') and
            all(command['exit_code'] == 0 for command in prepare['commands']), 'Successful immutable actual GN preparation required')
    database = json.loads((PREPARE / 'actual-compile-database.log').read_text())
    support_database = json.loads((SUPPORT / 'actual-compile-database.log').read_text())
    final = shlex.split(prepare['link_command'])  # Read-only graph evidence; never execute this string.
    published = MODULE / 'overlay/tools/gtos_isolate_diagnostic/BUILD.gn'
    bind(published)
    bind(PREPARE / 'source/tools/gtos_isolate_diagnostic/BUILD.gn', sha(published))
    payload = json.loads((MODULE / 'payload-lock.json').read_text())
    for relative, record in payload['files'].items():
        if relative.startswith('overlay/tools/gtos_isolate_diagnostic/native-scanf/'):
            bind(MODULE / relative, record['sha256'])
            bind(PREPARE / 'source' / relative[len('overlay/'):], record['sha256'])
    compiled = json.loads((COMPILE / 'status.json').read_text())
    require(compiled['actual_gn_commands_used'] and compiled['all_compile_pass'] and compiled['network_disabled'] and
            not compiled['shell_execution'] and compiled['all_consumed_inputs_bound'] and
            compiled['exit_code'] == 0 and compiled['object'] == str(SCANNER) and
            compiled['object_sha256'] == SCANNER_SHA, 'Successful actual scanner object build required')
    scanner_name = 'obj/third_party/llvm-libc/src/src/stdio/native_scanf_libc.sscanf.o'
    scanner_rows = [row for row in database if row.get('output') == scanner_name]
    require(len(scanner_rows) == 1 and scanner_name in final and str(SCALAR) in final,
            'Scanner and integer builtins must be genuine complete-target GN inputs')
    require(scanner_rows[0]['directory'] == str(PREPARE / 'build'),
            'Use the actual GN prepare recorded by the scanner; copied or rebased command metadata is not accepted')
    require(shlex.split(scanner_rows[0]['command']) == compiled['actual_gn_argv'], 'Scanner object no longer matches actual GN argv')
    bind(COMPILE / 'sscanf.o.d')
    bind(COMPILE / 'sscanf.su')
    for path in dependencies(COMPILE / 'sscanf.o.d', PREPARE / 'build'):
        # Preserve the original compiler's absolute dependency identities. A new
        # owned prepare requires a real producer or separately reviewed role/SHA
        # equivalence proof; this replay does not erase paths or ignore flags.
        require(str(path) in compiled['inputs'], 'Unbound scanner dependency: ' + str(path))
        bind(path, compiled['inputs'][str(path)])
    support_objects = []
    support_evidence = []
    for name in SUPPORT_NAMES:
        current_rows = [row for row in database if row.get('output') == name]
        old_rows = [row for row in support_database if row.get('output') == name]
        require(len(current_rows) == len(old_rows) == 1 and name in final and
                current_rows[0]['command'] == old_rows[0]['command'], 'Changed GN support-object compile input: ' + name)
        current_source = (PREPARE / 'build' / current_rows[0]['file']).resolve(strict=True)
        old_source = (SUPPORT / 'build' / old_rows[0]['file']).resolve(strict=True)
        bind(current_source, sha(old_source))
        bind(old_source)
        path = SUPPORT / 'build' / name
        digest = prior['inputs'].get(str(path.resolve()))
        require(digest is not None, 'Support object not qualified by prior public replay')
        bind(path, digest)
        if path.with_suffix('.su').is_file():
            bind(path.with_suffix('.su'))
        support_objects.append(path)
        support_evidence.append({'gn_output': name, 'object': str(path), 'object_sha256': digest,
                                 'compile_argv_identical': True, 'source_sha256': sha(current_source)})
    state['actual_gn_scanner_argv'] = compiled['actual_gn_argv']
    state['actual_gn_object_sha256'] = SCANNER_SHA
    state['reused_qualified_scanner_object'] = True
    state['input_manifest_sha256'] = args.input_manifest_sha256
    state['provided_source_lock_sha256'] = manifest['source_lock_sha256']
    state['consumed_source_inputs_manifest_bound'] = True
    state['reused_qualified_support_objects'] = support_evidence
    state['scanner_dependencies_bound'] = True
    state['executed_link_inputs_sha256'] = {str(path): sha(path) for path in (SCANNER, *support_objects, SCALAR)}
    heap_header = PREPARE / 'source/tools/gtos_isolate_diagnostic/runtime/heap.h'
    bind(heap_header)
    require(re.search(r'^#define GTOS_NATIVE_HEAP_BYTES 65536U$', heap_header.read_text(), re.M), 'Heap capacity must stay 64 KiB')
    run([CXX, '-dumpfullversion'], OUT / 'gcc-version.log')
    require((OUT / 'gcc-version.log').read_text().strip() == '13.3.0', 'Qualified GCC version changed')
    app = OUT / 'app'
    app.mkdir()
    # Probe-only consumer: explicit ABI/SDK settings, not a log-derived compile command.
    sdk = PREPARE / 'source/tools/gtos_isolate_diagnostic'
    main_argv = [CLANG, '-x', 'c++', '--target=i686-unknown-none-elf', '-ffreestanding',
                 '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fno-unwind-tables',
                 '-fno-asynchronous-unwind-tables', '-mno-mmx', '-msse2', '-mfpmath=sse',
                 '-mno-sse3', '-mno-ssse3', '-mno-sse4', '-mno-avx', '-femulated-tls',
                 '-fstack-usage', '-nostdinc', '-nostdinc++', '-fno-exceptions', '-fno-rtti',
                 '-DGTOS_ISOLATE_COMPILE_DIAGNOSTIC=1', '-D__GTOS__=1',
                 '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE',
                 '-isystem', sdk / 'sdk', '-isystem', PREPARE / 'source/third_party/libc++/src/include',
                 '-isystem', sdk / 'c-sdk', '-isystem', RESOURCE,
                 '-I' + str(SOURCE), '-I' + str(sdk / 'runtime'), '-I' + str(sdk / 'runtime/sdk'),
                 '-I' + str(PREPARE / 'source/third_party/llvm-libc/src'),
                 '-fno-builtin', '-std=c++20', '-O2', '-fdata-sections', '-ffunction-sections',
                 '-Wall', '-Wextra', '-Werror', '-MD', '-MF', app / 'main.d',
                 '-fcrash-diagnostics-dir=' + str(OUT / 'tmp'), '-c', SOURCE / 'main.cc', '-o', app / 'main.o']
    run(main_argv, app / 'compile.log', cwd=PREPARE / 'build')
    for path in dependencies(app / 'main.d', PREPARE / 'build'):
        bind(path)
    flags = ['-m32', '-std=c++11', '-ffreestanding', '-nostdlib', '-nostdinc', '-fno-builtin',
             '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-pie', '-fno-threadsafe-statics',
             '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections',
             '-fstack-usage', '-Iinclude', '-I' + str(REPO), '-D__GTOS__=1', '-U__linux__', '-U__unix__',
             '-Ulinux', '-Uunix', '-Wall', '-Wextra', '-Werror', '-O2',
             '-mgeneral-regs-only', '-mno-sse', '-mno-mmx', '-msoft-float', '-fno-tree-vectorize']
    bind(REPO / 'tools/kernel-cxxflags')
    require((REPO / 'tools/kernel-cxxflags').read_text().split() == flags[-5:], 'Kernel integer-only policy changed')
    start = REPO / 'apps/native_process_info_probe/start.s'
    bind(start)
    run([TOOLS['as'], '--32', start, '-o', app / 'start.o'], app / 'start.log')
    run([TOOLS['ld'], '-melf_i386', '--gc-sections', '-T', SOURCE / 'linker.ld',
         '-o', app / 'probe.elf', app / 'start.o', app / 'main.o', SCANNER, *support_objects, SCALAR], app / 'link.log')
    require(not run([TOOLS['nm'], '-u', app / 'probe.elf'], app / 'undefined.log').strip(), 'Native ELF has undefined symbols')
    symbols = run([TOOLS['nm'], '-n', app / 'probe.elf'], app / 'symbols.log')
    require(re.search(r'^40100000 [Dd] native_diag_record$', symbols, re.M) and
            re.search(r'^[0-9a-fA-F]+ T sscanf$', symbols, re.M), 'Real scanner and fixed record missing from ELF')
    header = run([TOOLS['readelf'], '-h', '-l', app / 'probe.elf'], app / 'readelf.log')
    require('Intel 80386' in header and 'EXEC' in header, 'Real IA32 executable required')
    run([TOOLS['objdump'], '-d', '-C', app / 'probe.elf'], app / 'disassembly.log')
    run([TOOLS['objcopy'], '--strip-all', app / 'probe.elf', app / 'probe.stripped.elf'], app / 'strip.log')
    elf = (app / 'probe.stripped.elf').read_bytes()
    require(len(elf) <= 1048576 and elf[:7] == b'\x7fELF\x01\x01\x01', 'Bounded real ELF32 required')
    phoff = struct.unpack_from('<I', elf, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', elf, 42)
    require(phsize == 32 and phoff + phsize * phcount <= len(elf), 'Bounded ELF program table required')
    pages, tables = set(), set()
    for index in range(phcount):
        kind, _, address, _, _, size, _, _ = struct.unpack_from('<8I', elf, phoff + index * phsize)
        if kind == 1 and size:
            pages.update(range(address // 4096, (address + size - 1) // 4096 + 1))
            tables.update(range(address >> 22, ((address + size - 1) >> 22) + 1))
    native_header = REPO / 'include/process/native_runtime.h'
    bind(native_header)
    stacks = re.findall(r'UserStackBottom\s*=\s*(0x[0-9A-Fa-f]+)', native_header.read_text())
    require(len(stacks) == 1 and int(stacks[0], 16) == 0xbfffd000, 'Native stack layout must stay unchanged')
    tables.add(int(stacks[0], 16) >> 22)
    require((0x80000000 >> 22) not in tables, 'Resident region must allocate one fresh directory table')
    frame_cost = len(pages) + 2 + len(tables) + 1 + 2
    state['expected_retained_frame_cost'] = frame_cost
    state['expected_cancel_retained_frame_cost'] = frame_cost + 16
    kernel = OUT / 'kernel'
    kernel.mkdir()
    names = ('src/gdt.cpp', 'src/multitasking.cpp', 'src/syscalls.cpp',
             'src/hardwarecommunication/interrupts.cpp', 'src/hardwarecommunication/port.cpp',
             'src/process/native_runtime.cpp', 'src/process/native_realtime.cpp', 'src/process/resources.cpp',
             'src/process/native_surface.cpp', 'src/process/native_fp.cpp', 'src/process/elf32.cpp',
             'src/memory/process_address_space.cpp', 'src/memory/paging.cpp', 'src/memory/physical.cpp', 'src/memory/bootstrap.cpp')
    kernel_objects = []
    for path in [REPO / name for name in names] + [SOURCE / 'kernel.cpp', REPO / 'apps/native_process_info_probe/memory.cc']:
        bind(path)
        obj = kernel / (path.stem + '.o')
        require(obj not in kernel_objects, 'Duplicate kernel object basename')
        kernel_objects.append(obj)
        run([CXX, *flags, '-Wno-write-strings', '-MD', '-MF', obj.with_suffix('.d'), '-c', path, '-o', obj],
            kernel / (path.stem + '.log'))
        for dependency in dependencies(obj.with_suffix('.d'), REPO):
            bind(dependency)
    for name, output in (('tests/native_process_loader.s', 'loader.o'),
                         ('src/process/native_fp.s', 'native_fp_asm.o'),
                         ('src/hardwarecommunication/interruptstubs.s', 'stubs.o')):
        bind(REPO / name)
        obj = kernel / output
        kernel_objects.append(obj)
        run([TOOLS['as'], '--32', REPO / name, '-o', obj], kernel / (output + '.log'))
    bind(REPO / 'tests/native_process_smoke.ld')
    audit = REPO / 'tools/audit-kernel-instructions.py'
    bind(audit)
    run([TOOLS['ld'], '-melf_i386', '--gc-sections', '-T', REPO / 'tests/native_process_smoke.ld',
         '-Map', OUT / 'kernel.map', '-o', OUT / 'kernel.bin', *kernel_objects], OUT / 'kernel-link.log')
    require(not run([TOOLS['nm'], '-u', OUT / 'kernel.bin'], OUT / 'kernel-undefined.log').strip(), 'Kernel has undefined symbols')
    run([PYTHON, '-I', '-B', audit, '--map', OUT / 'kernel.map', '--source-root', REPO, OUT / 'kernel.bin'],
        OUT / 'kernel-integer-audit.log')
    iso = OUT / 'iso'
    (iso / 'boot/grub').mkdir(parents=True)
    shutil.copyfile(OUT / 'kernel.bin', iso / 'boot/native.bin')
    shutil.copyfile(app / 'probe.stripped.elf', iso / 'boot/diag.elf')
    (iso / 'boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "Native scanf diagnostic" {\n multiboot /boot/native.bin\n module /boot/diag.elf\n boot\n}\n')
    run([TOOLS['grub-mkrescue'], '--output=' + str(OUT / 'diag.iso'), iso], OUT / 'grub.log')
    for memory, cpus in (('32M', 1), ('64M', 4)):
        guest = OUT / (memory + '-smp' + str(cpus))
        guest.mkdir()
        argv = [TOOLS['qemu-system-i386'], '-L', FIRMWARE, '-machine', 'pc', '-accel', 'tcg',
                '-cpu', 'max', '-m', memory, '-smp', str(cpus), '-cdrom', OUT / 'diag.iso', '-boot', 'd',
                '-nic', 'none', '-display', 'none', '-monitor', 'none', '-serial', 'none',
                '-debugcon', 'file:' + str(guest / 'guest.log'),
                '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
        emit(guest / 'invocation.json', {'argv': [str(item) for item in argv],
             'qemu_sha256': sha(TOOLS['qemu-system-i386']), 'iso_sha256': sha(OUT / 'diag.iso'),
             'kernel_sha256': sha(OUT / 'kernel.bin'), 'native_elf_sha256': sha(app / 'probe.stripped.elf'),
             'executed_scanner_object_sha256': sha(SCANNER), 'link_inputs_sha256': state['executed_link_inputs_sha256']})
        run(argv, guest / 'qemu.log', expected=33, timeout=300)
        observation = observe((guest / 'guest.log').read_text(), frame_cost)
        observation.update(memory=memory, cpus=cpus, guest_log_sha256=sha(guest / 'guest.log'))
        emit(guest / 'observations.json', observation)
        state['guests'].append(observation)
        print('PASS actual reviewed GN sscanf CPL3 ' + memory + '/smp' + str(cpus), flush=True)
    require(all(sha(path) == digest for path, digest in state['inputs'].items()), 'A consumed input changed during execution')
    state.update(all_required_checks_pass=True, native_scanf_guest_pass=True, source_before_after_identical=True,
                 actual_gn_object_executed=True, executed_scanner_object_sha256=sha(SCANNER),
                 exact_reap_checks=16, actual_victim_cases=14, normal_scanner_cases=164)
except BaseException as error:
    state['failure'] = str(error)
    if output_authorized:
        (OUT / 'exception.log').write_text(traceback.format_exc())
finally:
    state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    if output_authorized:
        emit(OUT / 'status.json', state)
    print(json.dumps({key: value for key, value in state.items() if key not in ('inputs', 'commands', 'guests',
                     'actual_gn_scanner_argv', 'reused_qualified_support_objects', 'executed_link_inputs_sha256')}, indent=2), flush=True)
sys.exit(0 if state['all_required_checks_pass'] else 1)

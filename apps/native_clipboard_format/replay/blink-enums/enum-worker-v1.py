#!/usr/bin/env python3
"""One of two fixed original Blink enum-schema producers; no generic dispatcher."""
import contextlib
import ctypes
import hashlib
import json
import os
import pathlib
import re
import runpy
import shlex
import sys
import time
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
P = W / 'artifacts/native-blink-enums-20261018-a'
PRIOR = W / 'artifacts/native-content-frontier-20261018-a'
SOURCE = P / 'source-v1'
RUNTIME = PRIOR / 'mojo-source-v1'
OUT = P / 'run-01'
PINS = P / 'pins-01.json'
PINS_SHA = 'ff3288a5b034217d344f74869d9cd652f03e13e1498ab2a27c6412715661c70e'
SCRIPTS = 'third_party/blink/renderer/build/scripts/'
CONFIG = 'third_party/blink/renderer/platform/runtime_enabled_features.json5'
OVERRIDE = 'third_party/blink/renderer/platform/runtime_enabled_features.override.json5'
GN = PRIOR / 'gn-clipboard-01'
ENV = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC'}
SPECS = {
    'origin-trials': ('origin_trials', 'generate_origin_trial_feature',
                      'make_origin_trial_feature_mojom_defs.py', 'origin_trial_feature'),
    'runtime-feature': ('runtime_feature_state', 'generate_runtime_feature',
                        'make_runtime_feature_mojom_defs.py', 'runtime_feature'),
}
state = {'complete': False, 'generation_pass': False, 'native_Content_pass': False,
         'browser_pass': False, 'inputs': {}, 'aliases': {}, 'staged_inputs': {},
         'outputs': {}, 'imported_modules': {}}
authorized = False
code = 1
began = time.monotonic()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def bind(path, digest, physical=False):
    logical = pathlib.Path(path)
    actual = logical.resolve(strict=True)
    require(W in actual.parents and actual.is_file() and sha(actual) == digest,
            'Input changed or escaped: ' + str(logical))
    require(not physical or actual == logical, 'Physical input alias forbidden')
    state['inputs'][str(actual)] = digest
    state['aliases'][str(logical)] = str(actual)


def emit():
    (stage_root / 'worker-status.json').write_text(json.dumps(state, indent=2) + '\n')


try:
    require(len(sys.argv) == 2 and sys.argv[1] in SPECS and sys.flags.isolated
            and sys.flags.dont_write_bytecode and sys.flags.no_site,
            'Exactly one of two fixed isolated worker stages required')
    require(pathlib.Path(__file__).resolve() == P / 'enum-worker-v1.py', 'Fixed worker file')
    name = sys.argv[1]
    directory, target, entry_name, basename = SPECS[name]
    stage_root = OUT / name
    build = stage_root / 'build'
    staged_source = stage_root / 'source'
    require(stage_root.is_dir() and stage_root.resolve() == stage_root
            and not build.exists() and not staged_source.exists(), 'Fresh fixed stage required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['launcher_sha256'] == sha(P / 'launch-enums-v1.py')
            and guard['child_sha256'] == sha(P / 'generate-enums-v1.py')
            and guard['landlock_applied'] and guard['landlock_abi'] >= 6
            and guard['network_disabled'] and guard['workspace_read_only']
            and guard['source_write_denied'] and guard['writable_output'] == str(OUT)
            and not guard['directory_execute_permissions']
            and not guard['output_symlink_creation_allowed'], 'Inherited exact guard required')
    require(all(os.stat('/proc/self/ns/' + key).st_ino == value
                for key, value in guard['namespace_ids'].items()), 'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1,
            'Inherited no_new_privs required')
    require(set(os.environ) <= {'PATH', 'LANG', 'TZ', 'LC_CTYPE'}
            and all(os.environ.get(key) == value for key, value in ENV.items()), 'Minimum environment')
    bind(PINS, PINS_SHA, physical=True)
    pins = json.loads(PINS.read_text())
    require(pins['revision'] == '04bca2584cddb39e134fc7c16bb2886e02863d0e'
            and len(pins['files']) == 51 and len(pins['declarations']) == 5
            and set(pins['actions']) == set(SPECS), 'Fixed original source contract')
    authorized = True
    (stage_root / 'worker-job.json').write_text(json.dumps({
        'state': 'running', 'stage': name, 'pid': os.getpid(), 'parent_pid': os.getppid(),
        'worker_sha256': sha(__file__), 'namespace_ids': guard['namespace_ids']}, indent=2) + '\n')
    bind(__file__, sha(__file__), physical=True)
    bind(P / 'generate-enums-v1.py', guard['child_sha256'], physical=True)
    bind(P / 'launch-enums-v1.py', guard['launcher_sha256'], physical=True)
    bind(pins['graph'], 'a57307df022644fc2807e927bdf9ce74a4c1304b6435c6b7495f14e8a7c45175')
    bind(pins['GN_receipt'], 'c3a702764baf8fcc8779b1fc49a3f80c7eaea146326c020269a886a791e3642f')
    bind(pins['runtime_manifest'], '84c80909f8c2ef503c0239bb342f0b6fc170dbf0c46edb616f10d50f03fd6d60')
    accepted = json.loads(pathlib.Path(pins['GN_receipt']).read_text())
    require(accepted['complete'] and accepted['typed_GN_dependency_qualification']
            and accepted['bound_inputs_unchanged'] and accepted['aliases_unchanged'], 'Qualified current GN')
    old = json.loads(pathlib.Path(pins['runtime_manifest']).read_text())
    copied = {}
    reused = 0
    for declaration, table in ((False, pins['files']), (True, pins['declarations'])):
        for relative, item in table.items():
            require(re.fullmatch(r'[A-Za-z0-9_./+-]+', relative)
                    and '..' not in pathlib.PurePosixPath(relative).parts
                    and item['mode'] in ('100644', '100755')
                    and re.fullmatch(r'[0-9a-f]{40}', item['blob'])
                    and item['kind'] == 'unchanged'
                    and item['raw_sha256'] == item['snapshot_sha256']
                    and item['raw'] == str(W / 'sources/chromium-full/src' / relative),
                    'Only fixed whole original Git bodies')
            bind(item['raw'], item['raw_sha256'], physical=True)
            bind(item['snapshot'], item['snapshot_sha256'], physical=True)
            bind(GN / 'source' / relative, item['raw_sha256'])
            if item['reused_runtime']:
                require(not declaration and item['snapshot'] == str(RUNTIME / relative)
                        and old['files'][relative]['kind'] == 'unchanged'
                        and old['files'][relative]['snapshot_sha256'] == item['snapshot_sha256']
                        and old['files'][relative]['blob'] == item['blob'], 'Qualified immutable runtime reuse')
                reused += 1
            else:
                require(item['snapshot'] == str(SOURCE / relative), 'Exact new source snapshot')
                copied[relative] = item['snapshot_sha256']
    require(reused == 29 and len(copied) == 27
            and pins['extra_implicit_import'] == SCRIPTS + 'blinkbuild/__init__.py', 'Complete import package')
    require({str(path.relative_to(SOURCE)) for path in SOURCE.rglob('*') if path.is_file()}
            == set(copied) and all(not path.is_symlink() for path in SOURCE.rglob('*')),
            'Exact new source snapshot inventory')

    entry = SCRIPTS + entry_name
    relative_output = 'gen/third_party/blink/public/mojom/' + directory + '/' + basename + '.mojom'
    output_dir = 'gen/third_party/blink/public/mojom/' + directory + '/'
    rule = '__third_party_blink_public_mojom_' + directory + '_' + target + '___build_toolchain_gtos_clang_x86__rule'
    wrapper_args = ['-I', '../source/third_party/blink/renderer/build/scripts',
                    '-I', '../source/third_party', '-I', '../source/third_party/pyjson5/src',
                    '-I', '../source/tools', '../source/' + entry,
                    '../source/' + CONFIG, '--output_dir', output_dir]
    action = pins['actions'][name]
    require(action['rule'] == rule and action['entry'] == entry
            and action['output'] == relative_output and action['output_dir'] == output_dir
            and action['template'] == SCRIPTS + 'templates/' + basename + '.mojom.tmpl'
            and action['typed_wrapper_arguments'] == wrapper_args, 'Independent fixed typed stage')
    normalized = re.sub(r'\$([$: ])', r'\1', action['command'])
    require(shlex.split(normalized) == [str(PRIOR / 'content-helper-clipboard-v1.py'),
            '../source/' + SCRIPTS + 'run_with_pythonpath.py'] + wrapper_args, 'Actual GN typed arguments')
    command = None
    row = None
    with pathlib.Path(pins['graph']).open() as stream:
        for line in stream:
            line = line.rstrip('\r\n')
            if line == 'rule ' + rule:
                require(command is None, 'Unique rule')
                command = next(stream).rstrip('\r\n')
            if line.startswith('build ' + relative_output + ':'):
                require(row is None, 'Unique whole-output build row')
                row = line
    require(command == '  command = ' + action['command'] and row is not None
            and hashlib.sha256(row.encode()).hexdigest() == action['build_row_sha256'], 'Actual graph production edge')
    prefix = 'build ' + relative_output + ': ' + rule + ' | '
    require(row.startswith(prefix), 'Whole unique declared output')
    inputs = row[len(prefix):].split()
    require(len(inputs) == 48 and len(set(inputs)) == 48
            and set(inputs) == {'../source/' + relative for relative in action['inputs']}
            and all(relative in pins['files'] for relative in action['inputs']), 'All actual source-only edge inputs pinned')

    build.mkdir()
    staged_source.mkdir()
    for relative in (CONFIG, OVERRIDE):
        destination = staged_source / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(pathlib.Path(pins['files'][relative]['snapshot']).read_bytes())
        state['staged_inputs'][str(destination)] = pins['files'][relative]['snapshot_sha256']
    os.chdir(build)
    original_path = list(sys.path)
    require(set(original_path) <= {'/usr/lib/python38.zip', '/usr/lib/python3.8',
                                  '/usr/lib/python3.8/lib-dynload'}
            and '/usr/lib/python3.8' in original_path, 'Only isolated standard import roots')
    sys.path[:0] = [str(SOURCE / 'third_party/blink/renderer/build/scripts'),
                    str(SOURCE / 'third_party/pyjson5/src'), str(RUNTIME / 'third_party')]
    argv = ['../source/' + CONFIG, '--output_dir', output_dir]
    state.update(stage=name, target=action['target'], argv=argv, original_GN_arguments_equal=True,
                 script=str(SOURCE / entry), wrapper_executed=False, independent_worker=True)
    emit()
    previous = list(sys.argv)
    sys.argv = [str(SOURCE / entry)] + argv
    with (stage_root / 'generator.log').open('w') as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
        try:
            runpy.run_path(str(SOURCE / entry), run_name='__main__')
        except SystemExit as result:
            require(result.code in (None, 0), 'Original main exited unsuccessfully')
        finally:
            sys.argv = previous

    for module_name, module in tuple(sys.modules.items()):
        filename = getattr(module, '__file__', None)
        if not filename:
            continue
        path = pathlib.Path(filename).resolve(strict=True)
        if W in path.parents:
            require(str(path) in state['inputs'] and str(path).endswith('.py'), 'Unbound workspace Python import: ' + str(path))
            state['imported_modules'][module_name] = str(path)
        else:
            require(str(path).startswith('/usr/lib/python3.8/'), 'Unexpected host import: ' + str(path))
    imported = state['imported_modules']
    require(all(module in imported for module in ('json5', 'json5.lib', 'json5.parser',
            'json5.version', 'jinja2', 'markupsafe._native', 'json5_generator',
            'make_runtime_features', 'make_runtime_features_utilities',
            'template_expander', 'blinkbuild', 'blinkbuild.name_style_converter'))
            and 'markupsafe._speedups' not in sys.modules, 'Complete original pure Python import closure')
    output = build / relative_output
    require(output.is_file() and not output.is_symlink() and output.resolve() == output,
            'Expected regular schema output')
    text = output.read_text()
    enum_name = 'OriginTrialFeature' if name == 'origin-trials' else 'RuntimeFeature'
    require(re.search(r'\bmodule\s+blink\.mojom\s*;', text)
            and re.search(r'\benum\s+' + enum_name + r'\s*\{', text)
            and 'Generated from template:' in text and action['template'][len(SCRIPTS):] in text
            and '../source/' + CONFIG in text and not re.search(r'^\s*import\s', text, re.M),
            'Actual original no-import schema and template provenance')
    body = re.search(r'\benum\s+' + enum_name + r'\s*\{([^}]*)\}\s*;', text, re.S)
    require(body is not None, 'Whole enum declaration')
    body_text = re.sub(r'//[^\n]*', '', body.group(1))
    members = re.findall(r'\bk[A-Za-z0-9_]+\b', body_text)
    require(members and len(set(members)) == len(members)
            and re.sub(r'\bk[A-Za-z0-9_]+\s*,', '', body_text).strip() == '', 'Complete unique enum entries')
    require(name != 'origin-trials' or (members[0] == 'kNonExisting' and len(members) > 1),
            'Original OriginTrialFeature sentinel and feature filtering')
    state['outputs'][relative_output] = sha(output)
    require({str(path.relative_to(build)) for path in build.rglob('*') if path.is_file()}
            == {relative_output} and all(not path.is_symlink() for path in stage_root.rglob('*')),
            'Only one exact production output and regular stage artifacts')
    require(all(sha(path) == digest for path, digest in state['inputs'].items())
            and all(str(pathlib.Path(path).resolve(strict=True)) == actual for path, actual in state['aliases'].items()),
            'Bound original inputs and aliases changed')
    require(all(sha(path) == digest for path, digest in state['staged_inputs'].items())
            and all(sha(build / relative) == digest for relative, digest in state['outputs'].items()),
            'Staged schema inputs or generated output changed')
    state.update(complete=True, generation_pass=True, inputs_unchanged=True, aliases_unchanged=True,
                 staged_inputs_unchanged=True, outputs_unchanged=True, source_inventory_exact=True,
                 original_generation_logic_retained=True, enum_name=enum_name, enum_members=members,
                 scope='Actual whole original Blink enum-schema producer only; no C++ bindings, native Content or browser claim.')
    code = 0
except BaseException as error:
    state.update(failure=str(error), traceback=traceback.format_exc())
    traceback.print_exc()
finally:
    if authorized:
        state['returncode'] = code
        state['seconds'] = time.monotonic() - began
        emit()
        (stage_root / 'worker-job.json').write_text(json.dumps({
            'state': 'completed', 'stage': name, 'pid': os.getpid(), 'parent_pid': os.getppid(),
            'worker_sha256': sha(__file__), 'returncode': code,
            'worker_status_sha256': sha(stage_root / 'worker-status.json')}, indent=2) + '\n')
sys.exit(code)

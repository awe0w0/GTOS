#!/usr/bin/env python3
"""Run two fixed independent offline Blink enum-schema workers."""
import ctypes
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
P = W / 'artifacts/native-blink-enums-20261018-a'
OUT = P / 'run-01'
WORKER = P / 'enum-worker-v1.py'
WORKER_SHA = '43db110f3d8116ecab9b4e673c2e5b64f77997a649e8791ba11ebf9c32c31469'
PYTHON = pathlib.Path('/usr/bin/python3')
PYTHON_SHA = '298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
PINS = P / 'pins-01.json'
PINS_SHA = 'ff3288a5b034217d344f74869d9cd652f03e13e1498ab2a27c6412715661c70e'
PREPARATION = P / 'preparation-result-01.json'
PREPARATION_SHA = '4b6500f61518afa92a67b5669764bd4ff52e6f5a1b31011f50da1f0a2b6a6478'
ENV = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC'}
STAGES = (
    ('origin-trials', 'gen/third_party/blink/public/mojom/origin_trials/origin_trial_feature.mojom'),
    ('runtime-feature', 'gen/third_party/blink/public/mojom/runtime_feature_state/runtime_feature.mojom'),
)
state = {'complete': False, 'generation_pass': False, 'native_Content_pass': False,
         'browser_pass': False, 'inputs': {}, 'aliases': {}, 'staged_inputs': {},
         'evidence': {}, 'stages': [], 'outputs': {}}
authorized = False
code = 1
active_process = None


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def bind(path, digest):
    path = pathlib.Path(path)
    actual = path.resolve(strict=True)
    require(actual == path and actual.is_file() and W in actual.parents and sha(actual) == digest,
            'Fixed physical controller input changed or escaped: ' + str(path))
    state['inputs'][str(actual)] = digest
    state['aliases'][str(path)] = str(actual)


def emit():
    (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')


try:
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode
            and sys.flags.no_site and pathlib.Path(__file__).resolve() == P / 'generate-enums-v1.py',
            'Fixed isolated controller required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__)
            and guard['launcher_sha256'] == sha(P / 'launch-enums-v1.py')
            and guard['landlock_applied'] and guard['landlock_abi'] >= 6
            and guard['network_disabled'] and guard['workspace_read_only']
            and guard['source_write_denied'] and guard['writable_output'] == str(OUT)
            and not guard['directory_execute_permissions']
            and not guard['output_symlink_creation_allowed'], 'Exact inherited guard required')
    require(all(os.stat('/proc/self/ns/' + key).st_ino == value
                for key, value in guard['namespace_ids'].items()), 'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1,
            'Inherited no_new_privs required')
    require(set(os.environ) <= {'PATH', 'LANG', 'TZ', 'LC_CTYPE'}
            and all(os.environ.get(key) == value for key, value in ENV.items()), 'Minimum environment required')
    require(pathlib.Path(sys.executable).resolve() == pathlib.Path('/usr/bin/python3.8')
            and sha(PYTHON) == PYTHON_SHA, 'Fixed qualified Python required')
    authorized = True
    bind(__file__, guard['child_sha256'])
    bind(P / 'launch-enums-v1.py', guard['launcher_sha256'])
    bind(WORKER, WORKER_SHA)
    bind(PINS, PINS_SHA)
    bind(PREPARATION, PREPARATION_SHA)
    bind(P / 'prepare-pins-v1.ps1', '48313fc3ed41d5e61270e5e6daa088d58c245924d7fe85f36f4824d509a49f0d')
    preparation = json.loads(PREPARATION.read_text())
    require(preparation['complete'] and preparation['inputs_unchanged']
            and preparation['pins_sha256'] == PINS_SHA
            and preparation['Git_original_blobs_verified'] == 56
            and preparation['production_import_files'] == 51
            and preparation['direct_declarations'] == 5
            and preparation['reused_original_runtime_files'] == 29
            and preparation['new_snapshot_files'] == 27
            and not preparation['executed_project_scripts'] and not preparation['network_operations'],
            'Exact nonexecuting source preparation required')
    pins = json.loads(PINS.read_text())
    require(set(pins['actions']) == {name for name, output in STAGES}, 'Only two fixed schema stages')
    emit()
    for name, output in STAGES:
        root = OUT / name
        require(not root.exists() and not root.is_symlink(), 'Never overwrite an existing stage')
        require(pins['actions'][name]['output'] == output and sha(WORKER) == WORKER_SHA,
                'Fixed worker and stage unchanged before execution')
        root.mkdir()
        argv = [str(PYTHON), '-I', '-B', '-S', str(WORKER), name]
        entry = {'stage': name, 'argv': argv, 'shell': False, 'worker_sha256': WORKER_SHA,
                 'output': output, 'state': 'starting'}
        state['stages'].append(entry)
        emit()
        began = time.monotonic()
        with (root / 'worker.log').open('wb') as stream:
            process = subprocess.Popen(argv, cwd=W, env=ENV, shell=False, close_fds=True,
                                       stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
            active_process = process
            entry.update(state='running', pid=process.pid)
            (root / 'child-job.json').write_text(json.dumps({
                'state': 'running', 'stage': name, 'pid': process.pid, 'parent_pid': os.getpid(),
                'worker_sha256': WORKER_SHA}, indent=2) + '\n')
            emit()
            try:
                result = process.wait(timeout=600)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                result = 124
            active_process = None
            entry.update(state='completed', returncode=result, seconds=time.monotonic() - began)
            (root / 'child-job.json').write_text(json.dumps({
                'state': 'completed', 'stage': name, 'pid': process.pid, 'parent_pid': os.getpid(),
                'worker_sha256': WORKER_SHA, 'returncode': result}, indent=2) + '\n')
        emit()
        require(result == 0, 'Fixed schema worker failed: ' + name)
        worker_state_path = root / 'worker-status.json'
        worker_job_path = root / 'worker-job.json'
        worker_state = json.loads(worker_state_path.read_text())
        worker_job = json.loads(worker_job_path.read_text())
        require(worker_job['state'] == 'completed' and worker_job['pid'] == process.pid
                and worker_job['parent_pid'] == os.getpid() and worker_job['stage'] == name
                and worker_job['returncode'] == 0 and worker_job['worker_sha256'] == WORKER_SHA
                and worker_job['worker_status_sha256'] == sha(worker_state_path), 'Own worker terminal proof required')
        require(worker_state['complete'] and worker_state['generation_pass']
                and worker_state['returncode'] == 0 and worker_state['stage'] == name
                and worker_state['inputs_unchanged'] and worker_state['aliases_unchanged']
                and worker_state['staged_inputs_unchanged'] and worker_state['outputs_unchanged']
                and worker_state['source_inventory_exact'] and worker_state['original_generation_logic_retained']
                and worker_state['independent_worker'] and worker_state['original_GN_arguments_equal']
                and not worker_state['wrapper_executed'] and set(worker_state['outputs']) == {output}
                and not worker_state['native_Content_pass'] and not worker_state['browser_pass'],
                'Full actual producer postchecks required')
        for path, digest in worker_state['inputs'].items():
            require(path not in state['inputs'] or state['inputs'][path] == digest, 'Inconsistent worker input digest')
            state['inputs'][path] = digest
        for path, actual in worker_state['aliases'].items():
            require(path not in state['aliases'] or state['aliases'][path] == actual, 'Inconsistent worker input alias')
            state['aliases'][path] = actual
        require(set(worker_state['staged_inputs']) == {
            str(root / 'source/third_party/blink/renderer/platform/runtime_enabled_features.json5'),
            str(root / 'source/third_party/blink/renderer/platform/runtime_enabled_features.override.json5')},
            'Only two exact staged configuration inputs')
        state['staged_inputs'].update(worker_state['staged_inputs'])
        actual_output = root / 'build' / output
        digest = worker_state['outputs'][output]
        require(actual_output.resolve(strict=True) == actual_output and sha(actual_output) == digest,
                'Actual schema output changed after worker exit')
        state['outputs'][str(actual_output)] = digest
        entry.update(qualified=True, worker_status_sha256=sha(worker_state_path),
                     worker_job_sha256=sha(worker_job_path), worker_log_sha256=sha(root / 'worker.log'),
                     generator_log_sha256=sha(root / 'generator.log'), enum_name=worker_state['enum_name'],
                     enum_members=len(worker_state['enum_members']), imported_modules=worker_state['imported_modules'])
        for path in (worker_state_path, worker_job_path, root / 'worker.log',
                     root / 'generator.log', root / 'child-job.json'):
            state['evidence'][str(path)] = sha(path)
        emit()
        print(json.dumps({'stage': name, 'returncode': result,
                          'output_sha256': digest, 'enum_members': entry['enum_members']}), flush=True)
    require(len(state['outputs']) == 2 and len(state['stages']) == 2
            and all(entry.get('qualified') for entry in state['stages']), 'Both independent stages qualified')
    require(all(sha(path) == digest for path, digest in state['inputs'].items())
            and all(str(pathlib.Path(path).resolve(strict=True)) == actual for path, actual in state['aliases'].items()),
            'Bound original input or alias changed after both stages')
    require(all(sha(path) == digest for path, digest in state['staged_inputs'].items())
            and all(sha(path) == digest for path, digest in state['evidence'].items())
            and all(sha(path) == digest for path, digest in state['outputs'].items())
            and all(not path.is_symlink() for path in OUT.rglob('*')), 'Final outputs changed or symlink introduced')
    state.update(complete=True, generation_pass=True, inputs_unchanged=True, aliases_unchanged=True,
                 staged_inputs_unchanged=True, evidence_unchanged=True, outputs_unchanged=True,
                 all_workers_terminal=True, independent_python_workers=2,
                 original_GN_producer_stages=2, qualified_schema_outputs=2,
                 scope='Two actual original Blink enum-schema producers. Independent fixed isolated Python workers, original JSON5/override/templates/imports, complete postchecks. No Mojo C++ production, native Content execution, browser/web/video/HTML5 claim.')
    code = 0
except BaseException as error:
    state.update(failure=str(error), traceback=traceback.format_exc())
    traceback.print_exc()
finally:
    if active_process is not None:
        if active_process.poll() is None:
            active_process.kill()
        cleanup_result = active_process.wait()
        state['aborted_worker'] = {'stage': name, 'pid': active_process.pid,
                                   'returncode': cleanup_result, 'terminal': True}
        (OUT / name / 'child-job.json').write_text(json.dumps({
            'state': 'completed', 'stage': name, 'pid': active_process.pid,
            'parent_pid': os.getpid(), 'worker_sha256': WORKER_SHA,
            'returncode': cleanup_result, 'aborted_after_controller_failure': True}, indent=2) + '\n')
    if authorized:
        state['returncode'] = code
        emit()
sys.exit(code)

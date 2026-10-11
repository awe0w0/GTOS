#!/usr/bin/env python3
"""Reproduce the pinned complete GTOS IA32 V8 executable in a fresh directory."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import signal
import struct
import subprocess
import time
import traceback


repo = pathlib.Path(__file__).resolve().parents[1]
module = repo / 'apps/native_v8_integration'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--v8-source', required=True, type=pathlib.Path)
parser.add_argument('--clang', required=True, type=pathlib.Path)
parser.add_argument('--gn', required=True, type=pathlib.Path)
parser.add_argument('--ninja', required=True, type=pathlib.Path)
parser.add_argument('--host-i386-runtime', required=True, type=pathlib.Path)
parser.add_argument('--integer-builtins', required=True, type=pathlib.Path)
parser.add_argument('--enable-diagnostic', action='store_true')
parser.add_argument('--prepare-only', action='store_true')
parser.add_argument('--cache-from', type=pathlib.Path)
parser.add_argument('--jobs', type=int, default=8)
parser.add_argument('--build-timeout', type=int, default=7200)
args = parser.parse_args()
assert os.name == 'posix', 'Run with the pinned Linux build host, including WSL'
assert args.enable_diagnostic, 'This limited profile requires --enable-diagnostic'
assert 1 <= args.jobs <= 16
assert 1 <= args.build_timeout <= 86400
out = args.output.resolve()
upstream = args.v8_source.resolve()
assert not out.exists(), 'Choose a fresh output; existing caches are never removed'
assert out != upstream and upstream not in out.parents
assert out not in upstream.parents, 'The output must not contain the source checkout'
for path in (out, upstream, args.clang.resolve(), args.gn.resolve(), args.ninja.resolve(),
             args.host_i386_runtime.resolve(), args.integer_builtins.resolve()):
    assert re.fullmatch(r'[A-Za-z0-9_./+:-]+', str(path)), 'GN shell tool paths require no spaces or shell metacharacters'
out.mkdir(parents=True)
root = out / 'source'
build = out / 'build'
state = dict(scope='Complete real IA32 GTOS native_embedder; retained build failures are not guest acceptance',
             commands=[], inputs={}, prepared=False, gn_pass=False, complete_build_pass=False,
             full_elf_exists=False, native_isolate_pass=False, javascript_guest_pass=False,
             browser_guest_pass=False, video_guest_pass=False, html5_guest_pass=False,
             signal_handlers_supported=False, production_image_changed=False, capacity_changed=False,
             cold_build=not args.prepare_only and args.cache_from is None)
environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', GIT_OPTIONAL_LOCKS='0')


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def bind(path, expected=None):
    path = pathlib.Path(path).resolve()
    actual = sha(path)
    if expected is not None:
        assert actual == expected, 'Changed pinned input: ' + str(path)
    previous = state['inputs'].get(str(path))
    assert previous is None or previous == actual, 'Input changed during preparation'
    state['inputs'][str(path)] = actual


def save():
    state['timestamp_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'status.json').write_text(json.dumps(state, indent=2) + '\n')


def run(name, command, timeout=180, cwd=None):
    command = [str(item) for item in command]
    log = out / (name + '.log')
    assert not log.exists()
    before = time.monotonic()
    print('RUN ' + name, flush=True)
    with log.open('wb') as stream:
        process = subprocess.Popen(command, cwd=cwd or root, env=environment,
                                   stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        state['live_command'] = dict(name=name, pid=process.pid, pgid=process.pid,
                                     started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        save()

        def owned_session_groups():
            groups = set()
            for stat in pathlib.Path('/proc').glob('[0-9]*/stat'):
                try:
                    fields = stat.read_text().rsplit(')', 1)[1].split()
                    if int(fields[3]) == process.pid and fields[0] != 'Z':
                        groups.add(int(fields[2]))
                except (FileNotFoundError, ProcessLookupError):
                    pass
            return groups

        def stop_owned_group():
            # Ninja may give each compiler job a separate process group.
            # All normal compiler jobs inherit this explicitly created session.
            for group in owned_session_groups():
                try:
                    os.killpg(group, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
            for group in owned_session_groups():
                try:
                    os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait()

        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            stop_owned_group()
            code = 124
        except BaseException:
            stop_owned_group()
            raise
        finally:
            state.pop('live_command', None)
    state['commands'].append(dict(name=name, argv=command, cwd=str(cwd or root),
                                  exit_code=code, elapsed_seconds=time.monotonic() - before,
                                  log=str(log), log_sha256=sha(log), pid=process.pid, pgid=process.pid))
    save()
    print('END ' + name + ' exit=' + str(code), flush=True)
    return code


def private_parent(relative):
    parts = pathlib.PurePosixPath(relative).parts
    assert parts and not pathlib.PurePosixPath(relative).is_absolute() and '..' not in parts
    current = root
    for part in parts[:-1]:
        current = current / part
        if current.is_symlink():
            original = current.resolve(strict=True)
            assert original.is_dir()
            current.unlink()
            current.mkdir()
            for child in original.iterdir():
                if child.name not in ('.git', '__pycache__', 'out'):
                    (current / child.name).symlink_to(child, target_is_directory=child.is_dir())
        else:
            current.mkdir(exist_ok=True)
        assert os.path.commonpath([str(current.resolve()), str(root.resolve())]) == str(root.resolve())
    return root.joinpath(*parts)


try:
    bind(module / 'source-lock.json')
    lock = json.loads((module / 'source-lock.json').read_text())
    payload_path = module / 'payload-lock.json'
    bind(payload_path, lock['payload_lock_sha256'])
    bind(pathlib.Path(__file__), lock['builder_sha256'])
    payload = json.loads(payload_path.read_text())
    actual_payload = set()
    for directory in ('overlay', 'sdk', 'c-sdk'):
        assert not (module / directory).is_symlink()
        for path in (module / directory).rglob('*'):
            assert not path.is_symlink(), 'Unlocked SDK/overlay symlink: ' + str(path)
            if path.is_file():
                actual_payload.add(str(path.relative_to(module)))
    expected_payload = {name for name in payload['files'] if name.startswith(('overlay/', 'sdk/', 'c-sdk/'))}
    assert actual_payload == expected_payload, 'Unlocked or missing SDK/overlay files'
    for relative, record in payload['files'].items():
        pure = pathlib.PurePosixPath(relative)
        assert not pure.is_absolute() and '..' not in pure.parts
        path = module / relative
        assert not path.is_symlink()
        bind(path, record['sha256'])
    for relative, revision in lock['upstream_revisions'].items():
        checkout = upstream / relative
        head = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'],
                                       text=True, env=environment).strip()
        assert head == revision, 'Wrong upstream revision: ' + relative
        dirty = subprocess.check_output(['git', '-C', str(checkout), 'status', '--porcelain',
                                         '--untracked-files=no'], text=True, env=environment)
        assert not dirty, 'Tracked upstream changes must be isolated: ' + relative
    for relative, expected in lock['prepared_source_files'].items():
        bind(upstream / relative, expected)
    clang = args.clang.resolve()
    bind(clang.parent / 'clang++', lock['tool_sha256']['clang'])
    gn = args.gn.resolve()
    ninja = args.ninja.resolve()
    builtins = args.integer_builtins.resolve()
    host_runtime = args.host_i386_runtime.resolve()
    for name, path in (('clang', clang), ('gn', gn), ('ninja', ninja),
                       ('ld.lld', clang.parent / 'ld.lld'), ('llvm-ar', clang.parent / 'llvm-ar'),
                       ('integer_builtins', builtins)):
        bind(path, lock['tool_sha256'][name])
    for relative, expected in lock['clang_resource_files'].items():
        bind(clang.parent.parent / 'lib/clang/24/include' / relative, expected)
    for relative, expected in lock['host_i386_runtime_files'].items():
        bind(host_runtime / relative, expected)
    root.mkdir()
    for child in upstream.iterdir():
        if child.name not in ('.git', '__pycache__', 'out'):
            (root / child.name).symlink_to(child, target_is_directory=child.is_dir())
    for relative in payload['files']:
        if not relative.startswith('overlay/'):
            continue
        target = private_parent(relative[len('overlay/'):])
        if target.is_symlink():
            target.unlink()
        assert not target.exists(), 'Unexpected owned overlay collision: ' + str(target)
        shutil.copy2(module / relative, target)
    target_sdk = private_parent('tools/gtos_isolate_diagnostic/sdk')
    assert not target_sdk.exists()
    shutil.copytree(module / 'sdk', target_sdk)
    target_c_sdk = private_parent('tools/gtos_isolate_diagnostic/c-sdk')
    assert not target_c_sdk.exists()
    shutil.copytree(module / 'c-sdk', target_c_sdk)
    state['prepared'] = True
    if args.cache_from is not None:
        cache = args.cache_from.resolve()
        assert cache.is_dir() and cache != build
        previous_lock = cache.parent / 'module-input-snapshot/source-lock.json'
        assert previous_lock.is_file(), 'Cache reuse requires its retained module input snapshot'
        previous = json.loads(previous_lock.read_text())
        assert previous['upstream_revisions'] == lock['upstream_revisions']
        assert previous['tool_sha256'] == lock['tool_sha256']
        assert previous['host_i386_runtime_files'] == lock['host_i386_runtime_files']
        assert previous['clang_resource_files'] == lock['clang_resource_files']
        for name in ('.ninja_log', '.ninja_deps'):
            bind(cache / name)
        shutil.copytree(cache, build)
        assert os.stat(cache / '.ninja_log').st_ino != os.stat(build / '.ninja_log').st_ino
        state['cache_source'] = str(cache)
    gn_args = lock['gn_args'] + '\n' + '\n'.join((
        'gtos_clang_bin=' + json.dumps(str(clang.parent)),
        'clang_base_path=' + json.dumps(str(clang.parent.parent)),
        'gtos_host_i386_runtime=' + json.dumps(str(host_runtime)),
        'gtos_native_integer_builtins=' + json.dumps(str(builtins)),
    )) + '\n'
    (out / 'requested-args.gn').write_text(gn_args)
    code = run('gn-native-embedder', [gn, 'gen', build, '--root=' + str(root),
               '--dotfile=' + str(root / 'isolate-diagnostic.gn'),
               '--root-target=//tools/gtos_isolate_diagnostic:native_embedder',
               '--args=' + gn_args, '--fail-on-unused-args'], 300)
    assert code == 0, 'GN failed; this is not a successful complete-target replay'
    state['gn_pass'] = True
    assert run('gn-complete-dependencies', [gn, 'desc', build,
               '//tools/gtos_isolate_diagnostic:native_embedder', 'deps', '--all', '--format=json',
               '--root=' + str(root), '--dotfile=' + str(root / 'isolate-diagnostic.gn')]) == 0
    assert run('native-executable-commands', [ninja, '-C', build, '-t', 'commands', 'native_embedder']) == 0
    command_lines = (out / 'native-executable-commands.log').read_text().splitlines()
    links = [line for line in command_lines if 'ld.lld' in line and 'native_embedder.elf' in line]
    assert len(links) == 1, 'The complete target must include an actual ELF link'
    state['link_command'] = links[0]
    assert run('actual-compile-database', [ninja, '-C', build, '-t', 'compdb']) == 0
    if not args.prepare_only:
        code = run('complete-native-build', [ninja, '-C', build, '-k', '1', '-j',
                   str(args.jobs), 'native_embedder'], args.build_timeout)
        state['complete_build_pass'] = code == 0
        text = (out / 'complete-native-build.log').read_text(errors='replace')
        text = re.sub(r'\x1b\[[0-9;]*m', '', text)
        state['first_diagnostics'] = [line for line in text.splitlines()
                                      if 'error:' in line or line.startswith('FAILED:')]
        elf = build / 'native_embedder.elf'
        state['full_elf_exists'] = elf.is_file()
        if code == 0:
            header = elf.read_bytes()[:52]
            assert header[:7] == b'\x7fELF\x01\x01\x01'
            assert struct.unpack_from('<HH', header, 16) == (2, 3)
            state['elf_sha256'] = sha(elf)
    state['bound_inputs_unchanged'] = all(sha(path) == expected for path, expected in state['inputs'].items())
    assert state['bound_inputs_unchanged']
except BaseException as error:
    state['driver_failure'] = str(error)
    (out / 'driver-exception.log').write_text(traceback.format_exc())
save()
print(json.dumps({key: value for key, value in state.items()
                  if key not in ('commands', 'inputs', 'link_command')}, indent=2), flush=True)
raise SystemExit(0 if state['gn_pass'] and not state.get('driver_failure') and
                 (args.prepare_only or state['complete_build_pass']) else 1)

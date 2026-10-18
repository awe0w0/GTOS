#!/usr/bin/env python3
"""Two offline provider full-M checks; exact Python and Clang leaves."""
import atexit
import ctypes
import hashlib
import json
import os
import pathlib
import signal
import subprocess
import sys
import time

WORKSPACE = pathlib.Path('/mnt/f/GTOS-Chromium')
PACKET = WORKSPACE / 'artifacts/native-clipboard-format-20261018-a'
CHILD = PACKET / 'provider-source-v5.py'
CHILD_SHA = '062c638b18ab26af0a62a9e3eeac559cd39d51f32fc44c006a42cbdc70f05dc8'
CC = WORKSPACE / 'toolchains/chromium-clang-llvm24-62397f8b-57/bin/clang'
CC_SHA = '656e40312a04cf2fb2da46f98669416183b1ef49592bafb69ee6504443f0d2e7'
PYTHON = pathlib.Path('/usr/bin/python3')
PYTHON_SHA = '298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
EXECUTABLES = {
    PYTHON: PYTHON_SHA,
    CC: CC_SHA,
}
OUT = PACKET / 'provider-source-05'
SOURCE_DOCUMENT = CHILD
ENVIRONMENT = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC'}
RULE_PATH_HANDLES = []

def close_rule_path_handles():
    while RULE_PATH_HANDLES:
        os.close(RULE_PATH_HANDLES.pop())

atexit.register(close_rule_path_handles)

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def emit(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')

def reap_own_group(process, force):
    # The leader was created with start_new_session. Only this launcher's own
    # child processes in that one new group can be returned by waitpid here.
    if force and process.returncode is None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    result = process.wait()
    adopted = []
    while True:
        try:
            child, status = os.waitpid(-process.pid, os.WNOHANG)
        except ChildProcessError:
            break
        if child == 0:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child, status = os.waitpid(-process.pid, 0)
        adopted.append({'pid': child, 'wait_status': status})
    return result, adopted

class RulesetAttr(ctypes.Structure):
    _fields_ = [('handled_access_fs', ctypes.c_uint64),
                ('handled_access_net', ctypes.c_uint64), ('scoped', ctypes.c_uint64)]

class PathBeneathAttr(ctypes.Structure):
    _pack_ = 1
    _fields_ = [('allowed_access', ctypes.c_uint64), ('parent_fd', ctypes.c_int32)]

def landlock():
    require(os.uname().machine == 'x86_64', 'Reviewed Linux x86_64 syscall ABI required')
    libc = ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6', use_errno=True)
    libc.syscall.restype = ctypes.c_long
    abi = libc.syscall(ctypes.c_long(444), ctypes.c_void_p(), ctypes.c_size_t(0), ctypes.c_uint(1))
    require(abi >= 6, 'Landlock ABI >= 6 required; stop without fallback')
    handled = (1 << 16) - 1
    read_only = (1 << 2) | (1 << 3)
    # Fresh regular files only, no symlink/delete/rename/execute permission.
    output_access = read_only | (1 << 1) | (1 << 7) | (1 << 8) | (1 << 14)
    ruleset = RulesetAttr(handled, 3, 3)
    descriptor = libc.syscall(ctypes.c_long(444), ctypes.byref(ruleset), ctypes.sizeof(ruleset), ctypes.c_uint(0))
    require(descriptor >= 0, 'Cannot create Landlock ruleset: ' + str(ctypes.get_errno()))
    try:
        paths = [(WORKSPACE, read_only),
                 (pathlib.Path('/usr/bin'), read_only), (pathlib.Path('/usr/lib'), read_only),
                 (OUT, output_access), (pathlib.Path('/dev/null'), (1 << 1) | (1 << 2)),
                 (pathlib.Path('/etc/ld.so.cache'), 1 << 2),
                 (pathlib.Path('/dev/urandom'), 1 << 2), (pathlib.Path('/dev/random'), 1 << 2),
                 (pathlib.Path('/dev/zero'), 1 << 2)]
        for path in (pathlib.Path('/bin'), pathlib.Path('/lib'), pathlib.Path('/lib64')):
            paths.append((path.resolve(strict=True), read_only))
        for path in EXECUTABLES:
            paths.append((path.resolve(strict=True), (1 << 0) | (1 << 2)))
        paths.append((pathlib.Path('/lib64/ld-linux-x86-64.so.2').resolve(strict=True), (1 << 0) | (1 << 2)))
        for path, rights in paths:
            fd = os.open(str(path), os.O_PATH | os.O_CLOEXEC)
            retained = False
            try:
                rule = PathBeneathAttr(rights, fd)
                result = libc.syscall(ctypes.c_long(445), ctypes.c_int(descriptor), ctypes.c_int(1), ctypes.byref(rule), ctypes.c_uint(0))
                require(result == 0, 'Cannot add Landlock path rule: ' + str(ctypes.get_errno()))
                RULE_PATH_HANDLES.append(fd)
                retained = True
            finally:
                if not retained:
                    os.close(fd)
        require(libc.prctl(ctypes.c_int(38), ctypes.c_ulong(1), ctypes.c_ulong(0), ctypes.c_ulong(0), ctypes.c_ulong(0)) == 0, 'Cannot set no_new_privs')
        require(libc.syscall(ctypes.c_long(446), ctypes.c_int(descriptor), ctypes.c_uint(0)) == 0, 'Cannot apply Landlock: ' + str(ctypes.get_errno()))
    finally:
        os.close(descriptor)
    return abi

require(len(sys.argv) == 1, 'No tool/path/output overrides')
require(sys.flags.isolated and sys.flags.dont_write_bytecode and sys.flags.no_site and
        pathlib.Path(sys.executable).resolve() == pathlib.Path('/usr/bin/python3.8') and sha(PYTHON) == PYTHON_SHA,
        'Fixed qualified Python -I -B -S required')
require(set(os.environ) <= {'PATH', 'LANG', 'TZ', 'LC_CTYPE'} and
        all(os.environ.get(key) == value for key, value in ENVIRONMENT.items()), 'Minimum environment required')
interfaces = [line.split(':', 1)[0].strip() for line in pathlib.Path('/proc/self/net/dev').read_text().splitlines()[2:] if ':' in line]
require(interfaces == ['lo'] and not pathlib.Path('/proc/self/net/route').read_text().splitlines()[1:], 'Isolated network namespace required')
mapping = pathlib.Path('/proc/self/uid_map').read_text().split()
require(len(mapping) == 3 and mapping[0] == '0' and mapping[2] == '1' and os.getuid() == 0, 'Fresh mapped user namespace required')
require(WORKSPACE.is_dir() and PACKET.is_dir() and PACKET.resolve() == PACKET and
        pathlib.Path(__file__).resolve() == PACKET / 'launch-provider-source-v5.py', 'Fixed packet required')
require(CHILD.is_file() and not CHILD.is_symlink() and CHILD.resolve() == CHILD and sha(CHILD) == CHILD_SHA, 'Reviewed child changed')
require(sha(SOURCE_DOCUMENT) == CHILD_SHA, 'Fixed provider qualification source changed')
for path, digest in EXECUTABLES.items():
    require(path.is_file() and sha(path) == digest, 'Reviewed executable changed')
require(not OUT.exists() and not OUT.is_symlink(), 'Fresh output required; preserve previous runs')
libc = ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6', use_errno=True)
require(libc.prctl(ctypes.c_int(36), ctypes.c_ulong(1), ctypes.c_ulong(0),
                   ctypes.c_ulong(0), ctypes.c_ulong(0)) == 0, 'Own-descendant subreaper required')
subreaper = ctypes.c_int(0)
require(libc.prctl(ctypes.c_int(37), ctypes.byref(subreaper), ctypes.c_ulong(0),
                   ctypes.c_ulong(0), ctypes.c_ulong(0)) == 0 and subreaper.value == 1,
        'Own-descendant subreaper verification failed')
source_document_sha256_before = sha(SOURCE_DOCUMENT)
namespace_metadata = {}
for name in ('net', 'user'):
    metadata = os.stat('/proc/self/ns/' + name)
    namespace_metadata[name] = {'st_dev': metadata.st_dev, 'st_ino': metadata.st_ino}
OUT.mkdir()
abi = landlock()
try:
    source_fd = os.open(str(SOURCE_DOCUMENT), os.O_WRONLY)
except PermissionError:
    source_write_denied = True
else:
    os.close(source_fd)
    raise RuntimeError('Source write restriction was not enforced')
source_document_sha256_after = sha(SOURCE_DOCUMENT)
require(source_document_sha256_after == source_document_sha256_before, 'Source changed during refusal check')
guard = {
    'launcher_sha256': sha(__file__), 'child_sha256': CHILD_SHA,
    'python_path': str(PYTHON), 'python_sha256': PYTHON_SHA,
    'executable_files': {str(path.resolve(strict=True)): digest for path, digest in EXECUTABLES.items()},
    'elf_loader': str(pathlib.Path('/lib64/ld-linux-x86-64.so.2').resolve(strict=True)),
    'directory_execute_permissions': False,
    'landlock_applied': True, 'landlock_abi': abi,
    'landlock_handled_access_fs': 65535, 'landlock_handled_access_net': 3, 'landlock_scoped': 3,
    'workspace_read_only': True, 'writable_output': str(OUT),
    'source_write_denied': source_write_denied,
    'source_document_sha256_before': source_document_sha256_before,
    'source_document_sha256_after': source_document_sha256_after,
    'source_document_unchanged': True, 'rule_path_handles_retained': True,
    'artifact_execution_denied': True, 'output_artifact_execution_denied': True,
    'landlock_tcp_denied': True, 'landlock_abstract_socket_and_cross_domain_signal_scoped': True,
    'output_symlink_creation_allowed': False,
    'device_sink_exception': '/dev/null write-only sink; no IOCTL_DEV granted',
    'network_disabled': True, 'outer_user_namespace_uid_map': mapping,
    'minimum_child_environment': ENVIRONMENT, 'namespace_metadata': namespace_metadata,
    'own_child_subreaper': True, 'controller_starts_new_session': True,
    'namespace_ids': {name: metadata['st_ino'] for name, metadata in namespace_metadata.items()},
}
emit(OUT / 'guard.json', guard)
argv = [str(PYTHON), '-I', '-B', '-S', str(CHILD)]
began = time.monotonic()
adopted = []
failure = None
with (OUT / 'launcher.log').open('wb') as stream:
    process = subprocess.Popen(argv, cwd=WORKSPACE, env=ENVIRONMENT, shell=False, close_fds=True,
                               start_new_session=True, stdin=subprocess.DEVNULL,
                               stdout=stream, stderr=subprocess.STDOUT)
    try:
        emit(OUT / 'job.json', {'state': 'running', 'child_pid': process.pid, 'launcher_pid': os.getpid(),
                              'own_process_group': process.pid, 'child_sha256': CHILD_SHA,
                              'namespace_ids': guard['namespace_ids']})
        try:
            result = process.wait(timeout=2400)
        except subprocess.TimeoutExpired:
            ignored, adopted = reap_own_group(process, True)
            result = 124
        else:
            ignored, adopted = reap_own_group(process, False)
            if adopted:
                result = 1
    except BaseException as error:
        ignored, adopted = reap_own_group(process, True)
        result = 1
        failure = type(error).__name__ + ': ' + str(error)
    emit(OUT / 'job.json', {'state': 'completed', 'child_pid': process.pid, 'launcher_pid': os.getpid(),
                          'child_sha256': CHILD_SHA, 'namespace_ids': guard['namespace_ids'],
                          'own_process_group': process.pid, 'all_own_descendants_reaped': True,
                          'adopted_descendants': adopted, 'returncode': result, 'failure': failure})
emit(OUT / 'launcher-result.json', {'argv': argv, 'cwd': str(WORKSPACE), 'shell': False,
     'returncode': result, 'seconds': time.monotonic() - began, 'child_sha256': sha(CHILD),
     'all_own_descendants_reaped': True, 'adopted_descendants': adopted,
     'failure': failure,
     'guard_sha256': sha(OUT / 'guard.json'), 'launcher_log_sha256': sha(OUT / 'launcher.log')})
print(json.dumps({'returncode': result, 'output': str(OUT), 'landlock_abi': abi,
                  'launcher_log': str(OUT / 'launcher.log')}, indent=2), flush=True)
close_rule_path_handles()
sys.exit(result)

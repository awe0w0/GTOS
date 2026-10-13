#!/usr/bin/env python3
"""Run the parameterized scanner probe only after offline and Landlock guards."""
import argparse
import atexit
import ctypes
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time


parser = argparse.ArgumentParser(description='Confine the parameterized actual GN scanner object guest replay')
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
CHILD = PACKET / 'verify-native-scanf.py'
CHILD_SHA = 'a213dbe31c20a0629bb1188c78f226d2061f5ca60b206e4ccf4ddb01f0053834'
PYTHON = pathlib.Path('/usr/bin/python3')
PYTHON_SHA = '298a9e830ed52f36c299427565485d717d1ce0179c0597cc16560513eb780b06'
OUT = args.output.resolve()
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
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def emit(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


class RulesetAttr(ctypes.Structure):
    _fields_ = [('handled_access_fs', ctypes.c_uint64),
                ('handled_access_net', ctypes.c_uint64), ('scoped', ctypes.c_uint64)]


class PathBeneathAttr(ctypes.Structure):
    _pack_ = 1
    _fields_ = [('allowed_access', ctypes.c_uint64), ('parent_fd', ctypes.c_int32)]


def landlock():
    require(os.uname().machine == 'x86_64', 'Only reviewed Linux x86_64 syscall ABI is supported')
    libc = ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6', use_errno=True)
    libc.syscall.restype = ctypes.c_long
    abi = libc.syscall(ctypes.c_long(444), ctypes.c_void_p(), ctypes.c_size_t(0), ctypes.c_uint(1))
    require(abi >= 6, 'Landlock ABI >= 6 required; do not lower isolation or retry without it')
    # ABI >= 6 supports all sixteen currently handled FS rights, including
    # REFER, TRUNCATE and IOCTL_DEV. Only this fresh output gets mutation rights.
    handled = (1 << 16) - 1
    read_only = (1 << 2) | (1 << 3)
    executable = read_only | (1 << 0)
    output_access = read_only | (1 << 1) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 12) | (1 << 13) | (1 << 14)
    # Deny TCP bind/connect and cross-domain abstract sockets/signals too.
    ruleset = RulesetAttr(handled, 3, 3)
    descriptor = libc.syscall(ctypes.c_long(444), ctypes.byref(ruleset), ctypes.sizeof(ruleset), ctypes.c_uint(0))
    require(descriptor >= 0, 'Cannot create Landlock ruleset: errno ' + str(ctypes.get_errno()))
    try:
        # No /proc data-read rights. The child verifies its own namespace inode
        # metadata and the inherited no_new_privs setting through prctl.
        paths = [(WORKSPACE, read_only), (WORKSPACE / 'toolchains', executable),
                 (pathlib.Path('/usr'), executable), (OUT, output_access),
                 (pathlib.Path('/dev/null'), (1 << 1) | (1 << 2)),
                 (pathlib.Path('/etc/ld.so.cache'), 1 << 2),
                 (pathlib.Path('/dev/urandom'), 1 << 2), (pathlib.Path('/dev/random'), 1 << 2),
                 (pathlib.Path('/dev/zero'), 1 << 2)]
        for path in (pathlib.Path('/bin'), pathlib.Path('/lib'), pathlib.Path('/lib64')):
            paths.append((path.resolve(strict=True), executable))
        for path, rights in paths:
            fd = os.open(str(path), os.O_PATH | os.O_CLOEXEC)
            retained = False
            try:
                rule = PathBeneathAttr(rights, fd)
                result = libc.syscall(ctypes.c_long(445), ctypes.c_int(descriptor), ctypes.c_int(1),
                                      ctypes.byref(rule), ctypes.c_uint(0))
                require(result == 0, 'Cannot add Landlock path rule: errno ' + str(ctypes.get_errno()))
                # DrvFs requires a live path reference for the rule's inode.
                # Keep read-only O_PATH references in this parent; never inherit them.
                RULE_PATH_HANDLES.append(fd)
                retained = True
            finally:
                if not retained:
                    os.close(fd)
        libc.prctl.restype = ctypes.c_int
        require(libc.prctl(ctypes.c_int(38), ctypes.c_ulong(1), ctypes.c_ulong(0), ctypes.c_ulong(0), ctypes.c_ulong(0)) == 0,
                'Cannot set no_new_privs')
        require(libc.syscall(ctypes.c_long(446), ctypes.c_int(descriptor), ctypes.c_uint(0)) == 0,
                'Cannot apply Landlock restrictions: errno ' + str(ctypes.get_errno()))
    finally:
        os.close(descriptor)
    return abi


require(WORKSPACE != pathlib.Path('/') and args.workspace.is_absolute() and args.workspace == WORKSPACE,
        'Absolute canonical bounded workspace required')
require(all(re.fullmatch('[0-9a-f]{64}', value) for value in
            (args.input_manifest_sha256, args.tool_state_sha256)), 'Reviewed evidence SHA256 required')
directory_arguments = ('source_root', 'module_root', 'integration_build', 'support_build',
                       'scanner_compile', 'fixture_root', 'runtime_root', 'clang_resource_dir', 'firmware')
input_roots = []
for name in directory_arguments:
    raw = getattr(args, name)
    path = raw.resolve(strict=True)
    require(raw.is_absolute() and raw == path and path.is_dir() and WORKSPACE in path.parents,
            'Absolute canonical input directory inside workspace required: ' + name)
    input_roots.append(path)
for name in ('tool_state', 'input_manifest', 'integer_builtins'):
    path = getattr(args, name).resolve(strict=True)
    require(path.is_file() and WORKSPACE in path.parents, 'Controlling input outside workspace: ' + name)
require(sha(args.input_manifest) == args.input_manifest_sha256 and
        sha(args.tool_state) == args.tool_state_sha256, 'Reviewed manifest or tool state changed')
tool_state = json.loads(args.tool_state.read_text())
require(tool_state.get('all_required_checks_pass') is True and tool_state.get('network_disabled') is True,
        'Qualified offline tool state required')
for name in ('clang', 'cxx', 'qemu', 'assembler', 'linker', 'nm', 'objcopy', 'readelf', 'objdump', 'grub_mkrescue'):
    path = getattr(args, name).resolve(strict=True)
    require((WORKSPACE / 'toolchains' in path.parents or pathlib.Path('/usr') in path.parents) and
            tool_state['inputs'].get(str(path)) == sha(path), 'Unqualified explicit tool: ' + name)
runtime = args.runtime_root.resolve(strict=True)
require(WORKSPACE / 'toolchains' in runtime.parents and
        args.qemu.resolve(strict=True) == runtime / 'root/usr/bin/qemu-system-i386' and
        args.firmware.resolve(strict=True) == runtime / 'root/usr/share/qemu',
        'Fixed qualified runtime tool and firmware layout required')
require(args.output.is_absolute() and args.output == OUT and OUT.parent.is_dir() and
        WORKSPACE in OUT.parents and not any(root == OUT or root in OUT.parents for root in input_roots + [PACKET]),
        'Fresh output must stay outside every source, cache, tool and fixture directory')
require(not (OUT in args.input_manifest.resolve(strict=True).parents or OUT in args.tool_state.resolve(strict=True).parents),
        'Controlling evidence cannot be in writable output')
for parent in OUT.parents:
    require(not parent.is_symlink(), 'Output ancestor symlink forbidden')
    if parent == WORKSPACE:
        break
require(sys.flags.isolated and sys.flags.dont_write_bytecode and
        pathlib.Path(sys.executable).resolve() == pathlib.Path('/usr/bin/python3.8') and sha(PYTHON) == PYTHON_SHA,
        'Fixed qualified /usr/bin/python3 -I -B required')
require(set(os.environ) <= {'PATH', 'LANG', 'TZ', 'LC_CTYPE'} and
        all(os.environ.get(key) == value for key, value in ENVIRONMENT.items()), 'Minimum reviewed environment required')
interfaces = [line.split(':', 1)[0].strip() for line in pathlib.Path('/proc/self/net/dev').read_text().splitlines()[2:]
              if ':' in line]
require(interfaces == ['lo'] and not pathlib.Path('/proc/self/net/route').read_text().splitlines()[1:],
        'Outer unshare --user --map-root-user --net required; no network fallback')
mapping = pathlib.Path('/proc/self/uid_map').read_text().split()
require(len(mapping) == 3 and mapping[0] == '0' and mapping[2] == '1', 'Fresh outer user namespace required')
require(WORKSPACE.is_dir() and PACKET.is_dir() and PACKET.resolve() == PACKET and
        WORKSPACE in PACKET.parents and pathlib.Path(__file__).resolve() == PACKET / 'launch-native-scanf.py',
        'Named reviewed launcher inside workspace required')
require(CHILD.is_file() and not CHILD.is_symlink() and CHILD.resolve() == CHILD and sha(CHILD) == CHILD_SHA,
        'Reviewed child changed; stop for source review')
require(not OUT.exists() and not OUT.is_symlink(), 'Fresh output required; preserve previous runs')
OUT.mkdir()
namespace_ids = {name: os.stat('/proc/self/ns/' + name).st_ino for name in ('net', 'user')}
abi = landlock()

# Open without O_TRUNC or writes: prove source mutation remains forbidden.
try:
    source_fd = os.open(str(REPO / 'docs/native-command-execution.md'), os.O_WRONLY)
except PermissionError:
    source_write_denied = True
else:
    os.close(source_fd)
    raise RuntimeError('Source write restriction was not enforced')
guard = {
    'launcher_sha256': sha(__file__), 'child_sha256': CHILD_SHA,
    'python_path': str(PYTHON), 'python_sha256': PYTHON_SHA,
    'landlock_applied': True, 'landlock_abi': abi,
    'workspace_read_only': True, 'writable_output': str(OUT),
    'source_write_denied': source_write_denied,
    'rule_path_handles_retained': True,
    'artifact_execution_denied': True, 'landlock_tcp_denied': True,
    'landlock_abstract_socket_and_cross_domain_signal_scoped': True,
    'device_sink_exception': '/dev/null write-only sink; no IOCTL_DEV granted',
    'network_disabled': True, 'outer_user_namespace_uid_map': mapping,
    'minimum_child_environment': ENVIRONMENT,
    'namespace_ids': namespace_ids,
    'input_manifest_sha256': args.input_manifest_sha256,
    'tool_state_sha256': args.tool_state_sha256,
    'configuration_sha256': hashlib.sha256(json.dumps(sys.argv[1:], separators=(',', ':')).encode()).hexdigest(),
}
emit(OUT / 'guard.json', guard)
argv = [str(PYTHON), '-I', '-B', str(CHILD), *sys.argv[1:]]
began = time.monotonic()
with (OUT / 'launcher.log').open('wb') as stream:
    try:
        result = subprocess.run(argv, cwd=WORKSPACE, env=ENVIRONMENT, shell=False, close_fds=True,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=1800).returncode
    except subprocess.TimeoutExpired:
        result = 124
emit(OUT / 'launcher-result.json', {'argv': argv, 'cwd': str(WORKSPACE), 'shell': False,
     'returncode': result, 'seconds': time.monotonic() - began, 'child_sha256': sha(CHILD),
     'guard_sha256': sha(OUT / 'guard.json'), 'launcher_log_sha256': sha(OUT / 'launcher.log')})
print(json.dumps({'returncode': result, 'output': str(OUT), 'landlock_abi': abi,
                  'launcher_log': str(OUT / 'launcher.log')}, indent=2), flush=True)
close_rule_path_handles()
sys.exit(result)

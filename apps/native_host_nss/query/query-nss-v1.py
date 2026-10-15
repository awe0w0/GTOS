#!/usr/bin/env python3
"""Query only the authenticated NSS/NSPR metadata in two cached sysroots."""
import ctypes
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
P = W / 'artifacts/native-rust-bin-20261015-a'
OUT = P / 'nss-query-01'
UPSTREAM = W / 'sources/chromium-full/src'
AUDIT = P / 'host-sysroot-audit-01/status.json'
AUDIT_SHA = 'ddf14785a97044f7e2595d30d7dabf9cd628a68e80fb2fb6a49b167550df7841'
TOOL = pathlib.Path('/usr/bin/pkg-config')
TOOL_SHA = '229ad55026c5c2045ddb0a2b7772d629f488dd38278c982d5c67258a743ff8e3'
state = {'query_complete': False, 'commands': [], 'results': {},
         'gn_executed': False, 'target_compiled': False, 'target_executed': False,
         'certificate_configuration_changed': False, 'inputs': {}, 'aliases': {}}
authorized = False


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def bind(path, digest):
    logical = pathlib.Path(path)
    actual = logical.resolve(strict=True)
    require(sha(actual) == digest, 'Changed input: ' + str(logical))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)] == digest,
            'Conflicting input')
    state['inputs'][str(actual)] = digest
    if logical != actual:
        state['aliases'][str(logical)] = str(actual)


try:
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode
            and sys.flags.no_site, 'Fixed isolated no-site Python entry required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__) and
            guard['launcher_sha256'] == sha(P / 'launch-nss-query-v1.py') and
            guard['landlock_applied'] and guard['network_disabled'] and guard['source_write_denied'] and
            not guard['directory_execute_permissions'] and guard['writable_output'] == str(OUT) and
            guard['executable_files'].get(str(TOOL)) == TOOL_SHA, 'Guard mismatch')
    require(all(os.stat('/proc/self/ns/' + name).st_ino == guard['namespace_ids'][name]
                for name in ('user', 'net')), 'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1,
            'no_new_privs required')
    authorized = True
    bind(AUDIT, AUDIT_SHA)
    audit = json.loads(AUDIT.read_text())
    require(audit['audit_complete'] and audit['bound_inputs_unchanged'] and audit['input_aliases_unchanged'] and
            not audit['pkg_config_executed'] and len(audit['host_elf_files']) == 6 and
            audit['host_pkg_config_actual_path'] == str(TOOL) and audit['host_pkg_config_sha256'] == TOOL_SHA and
            'failure' not in audit, 'Completed read-only input audit required')
    for path, digest in audit['inputs'].items():
        bind(path, digest)
    for logical, actual in audit['aliases'].items():
        require(str(pathlib.Path(logical).resolve(strict=True)) == actual, 'Audited alias changed')
        state['aliases'][logical] = actual
    bind(TOOL, TOOL_SHA)
    proof_path = P / 'host-sysroot-inputs-01.json'
    proof = json.loads(proof_path.read_text())
    require(state['inputs'].get(str(proof_path)) == '2312fe136ce46079f5fe8e35389a77a17d2428b02d3b948f919ee8666f19bf95',
            'Sysroot provenance proof not bound')
    helper = UPSTREAM / 'build/config/linux/pkg-config.py'
    require(state['inputs'].get(str(helper)) == proof['files']['build/config/linux/pkg-config.py']['sha256'],
            'Reviewed helper not bound')
    for name in ('home', 'tmp'):
        (OUT / name).mkdir()
    environment = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'TZ': 'UTC',
                   'HOME': str(OUT / 'home'), 'TMPDIR': str(OUT / 'tmp')}
    for name, arch, triple in (('bullseye_i386', 'x86', 'i386-linux-gnu'),
                               ('bullseye_amd64', 'x64', 'x86_64-linux-gnu')):
        item = proof['sysroots'][name]
        root = UPSTREAM / item['directory']
        require(audit['sysroots'][name]['selected_installed_members_match_archive'] and
                audit['sysroots'][name]['archive_sha256'] == item['archive_sha256'],
                'Selected metadata lacks archive authentication')
        argv = ['/usr/bin/python3', '-I', '-B', '-S', str(helper), '-p', str(TOOL),
                '-s', str(root), '-a', arch, 'nss', '-v', '-lssl3']
        stdout = OUT / (name + '.stdout.json')
        stderr = OUT / (name + '.stderr.log')
        with stdout.open('xb') as output, stderr.open('xb') as error:
            result = subprocess.run(argv, cwd=OUT, env=environment, shell=False, close_fds=True,
                                    stdin=subprocess.DEVNULL, stdout=output, stderr=error, timeout=30)
        state['commands'].append({'name': name, 'argv': argv, 'returncode': result.returncode,
                                  'shell': False, 'stdout_sha256': sha(stdout), 'stderr_sha256': sha(stderr)})
        require(result.returncode == 0 and stderr.stat().st_size == 0, 'Exact NSS query failed: ' + name)
        values = json.loads(stdout.read_text())
        require(isinstance(values, list) and len(values) == 4 and
                all(isinstance(part, list) and all(isinstance(value, str) for value in part)
                    for part in values), 'Unexpected pkg-config output shape')
        require(values[0] == sorted([str(root / 'usr/include/nss'), str(root / 'usr/include/nspr')]) and
                values[1] == [] and values[2] == ['nspr4', 'nss3', 'nssutil3', 'plc4', 'plds4', 'smime3'] and
                values[3] in ([], [str(root / ('usr/lib/' + triple))]),
                'NSS metadata output differs from reviewed headers/libraries')
        require('ssl3' not in values[2], 'NSS SSL library unexpectedly retained')
        state['results'][name] = {'arch': arch, 'sysroot_relative': item['directory'],
                                  'pkgresult': values, 'ssl3_excluded': True}
    require(all(sha(path) == digest for path, digest in state['inputs'].items()), 'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True)) == actual
                for path, actual in state['aliases'].items()), 'Bound alias changed')
    state.update(query_complete=True, bound_inputs_unchanged=True, input_aliases_unchanged=True,
                 fixed_pkg_config_sha256=TOOL_SHA, package_closure=['nss', 'nspr'],
                 scope='Two fixed offline metadata queries only; no GN, compilation, target execution, TLS or trust-store runtime qualification')
    (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
    print(json.dumps({'query_complete': True, 'queries': len(state['commands']),
                      'bound_inputs_unchanged': True}, indent=2), flush=True)
except BaseException as error:
    if authorized:
        state.update(failure=str(error), traceback=traceback.format_exc())
        (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
    traceback.print_exc()
    sys.exit(1)

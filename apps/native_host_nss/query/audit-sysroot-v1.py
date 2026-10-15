#!/usr/bin/env python3
"""Read cached sysroot members and the fixed host pkg-config dependency graph."""
import ctypes
import hashlib
import json
import os
import pathlib
import re
import struct
import sys
import tarfile
import traceback

W = pathlib.Path('/mnt/f/GTOS-Chromium')
P = W / 'artifacts/native-rust-bin-20261015-a'
OUT = P / 'host-sysroot-audit-01'
UPSTREAM = W / 'sources/chromium-full/src'
INPUT = P / 'host-sysroot-inputs-01.json'
INPUT_SHA = '2312fe136ce46079f5fe8e35389a77a17d2428b02d3b948f919ee8666f19bf95'
state = {'audit_complete': False, 'pkg_config_executed': False,
         'installer_executed': False, 'gn_executed': False,
         'certificate_configuration_changed': False,
         'complete_pkg_config_runtime_qualification': False,
         'inputs': {}, 'aliases': {}, 'sysroots': {}, 'host_elf_files': {}}
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
    require(actual.is_file() and sha(actual) == digest, 'Changed input: ' + str(logical))
    require(str(actual) not in state['inputs'] or state['inputs'][str(actual)] == digest,
            'Conflicting input digest')
    state['inputs'][str(actual)] = digest
    if logical != actual:
        state['aliases'][str(logical)] = str(actual)


def cstring(data, begin, limit):
    require(0 <= begin < limit <= len(data), 'String range outside file')
    end = data.find(b'\0', begin, limit)
    require(end >= begin, 'Unterminated ELF string')
    return data[begin:end].decode('ascii')


def elf_dependencies(path):
    data = path.read_bytes()
    require(64 <= len(data) <= 32 * 1024 * 1024 and
            data[:7] == b'\x7fELF\x02\x01\x01', 'Reviewed host ELF64 little-endian required')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    require(header[1] in (2, 3) and header[2] == 62 and header[3] == 1 and
            header[8] == 64 and header[9] == 56 and 0 < header[10] <= 64,
            'Unexpected ELF64 header')
    require(header[5] + header[9] * header[10] <= len(data), 'Program table outside file')
    loads = []
    dynamic = None
    interpreter = None
    for index in range(header[10]):
        ptype, flags, offset, address, _, size, memsize, align = struct.unpack_from(
            '<IIQQQQQQ', data, header[5] + index * 56)
        require(offset + size <= len(data) and size <= memsize, 'ELF segment outside file')
        if ptype == 1:
            loads.append((address, offset, size))
        elif ptype == 2:
            require(dynamic is None and size % 16 == 0, 'Invalid dynamic segment')
            dynamic = (offset, size)
        elif ptype == 3:
            require(interpreter is None, 'Duplicate interpreter')
            interpreter = cstring(data, offset, offset + size)
    require(dynamic is not None, 'Expected dynamic host tool/library')
    tags = {}
    needed = []
    terminated = False
    for offset in range(dynamic[0], dynamic[0] + dynamic[1], 16):
        tag, value = struct.unpack_from('<qQ', data, offset)
        if tag == 0:
            terminated = True
            break
        if tag == 1:
            needed.append(value)
        elif tag in (5, 10, 15, 29):
            require(tag not in tags, 'Duplicate singleton dynamic tag')
            tags[tag] = value
    require(terminated and 5 in tags and 10 in tags and not (15 in tags or 29 in tags),
            'Missing strings or unreviewed RPATH/RUNPATH')
    mappings = [(offset + tags[5] - address, size - (tags[5] - address))
                for address, offset, size in loads if address <= tags[5] < address + size]
    require(len(mappings) == 1 and 0 < tags[10] <= mappings[0][1], 'Invalid dynamic string mapping')
    begin = mappings[0][0]
    names = [cstring(data, begin + value, begin + tags[10]) for value in needed]
    require(len(names) == len(set(names)) and
            all(re.fullmatch(r'[A-Za-z0-9_+.-]+\.so(?:\.[0-9]+)*', name) for name in names),
            'Unsafe or duplicate shared-library names')
    return {'sha256': sha(path), 'bytes': len(data), 'needed': names,
            'interpreter': interpreter, 'rpath': None, 'runpath': None}


try:
    require(len(sys.argv) == 1 and sys.flags.isolated and sys.flags.dont_write_bytecode
            and sys.flags.no_site, 'Fixed isolated no-site Python entry required')
    guard = json.loads((OUT / 'guard.json').read_text())
    require(guard['child_sha256'] == sha(__file__) and
            guard['launcher_sha256'] == sha(P / 'launch-sysroot-audit-v1.py') and
            guard['landlock_applied'] and guard['network_disabled'] and
            guard['source_write_denied'] and not guard['directory_execute_permissions'] and
            guard['writable_output'] == str(OUT), 'Guard mismatch')
    require(all(os.stat('/proc/self/ns/' + name).st_ino == guard['namespace_ids'][name]
                for name in ('user', 'net')), 'Namespace changed')
    require(ctypes.CDLL('/lib/x86_64-linux-gnu/libc.so.6').prctl(39, 0, 0, 0, 0) == 1,
            'no_new_privs required')
    authorized = True
    bind(INPUT, INPUT_SHA)
    proof = json.loads(INPUT.read_text())
    require(proof['revision'] == '04bca2584cddb39e134fc7c16bb2886e02863d0e' and
            len(proof['files']) == 9 and set(proof['sysroots']) == {'bullseye_i386', 'bullseye_amd64'} and
            not proof['cached_scope_has_i386'] and proof['cached_scope_has_amd64_nss'],
            'Sysroot audit input scope changed')
    for relative, item in proof['files'].items():
        bind(UPSTREAM / relative, item['sha256'])
    for name, item in proof['sysroots'].items():
        root = UPSTREAM / item['directory']
        archive_path = root / item['archive']
        require(root.resolve(strict=True) == root and
                archive_path.name == '.' + item['archive_sha256'], 'Unexpected cached archive path')
        bind(archive_path, item['archive_sha256'])
        require(set(item['selected']) == {'usr/lib/pkgconfig/nss.pc', 'usr/lib/pkgconfig/nspr.pc',
                                         'usr/include/nss/nss.h', 'usr/include/nspr/prtypes.h'},
                'Selected sysroot members changed')
        for relative, expected in item['selected'].items():
            bind(root / relative, expected['sha256'])
        found = {}
        # Stream the authenticated archive; never extract files or invoke its contents.
        with tarfile.open(str(archive_path), mode='r|xz') as archive:
            for member in archive:
                relative = member.name[2:] if member.name.startswith('./') else member.name
                if relative not in item['selected']:
                    continue
                require(relative not in found and member.isfile() and 0 < member.size <= 256 * 1024,
                        'Unexpected selected archive member shape')
                expected = item['selected'][relative]
                require(member.size == expected['bytes'], 'Installed/archive member size mismatch')
                stream = archive.extractfile(member)
                require(stream is not None, 'Missing selected archive payload')
                with stream:
                    payload = stream.read(256 * 1024 + 1)
                digest = hashlib.sha256(payload).hexdigest()
                require(len(payload) == member.size and digest == expected['sha256'],
                        'Installed/archive member bytes differ')
                found[relative] = {'sha256': digest, 'bytes': member.size, 'archive_member': member.name}
        require(set(found) == set(item['selected']), 'Selected archive members missing')
        state['sysroots'][name] = {'archive_sha256': item['archive_sha256'], 'members': found,
                                  'selected_installed_members_match_archive': True}
    tool = pathlib.Path('/usr/bin/pkg-config')
    actual_tool = tool.resolve(strict=True)
    require(actual_tool.parent == pathlib.Path('/usr/bin'), 'Unexpected pkg-config path')
    pending = [tool]
    seen = set()
    directories = (pathlib.Path('/lib/x86_64-linux-gnu'), pathlib.Path('/usr/lib/x86_64-linux-gnu'))
    while pending:
        logical = pending.pop()
        actual = logical.resolve(strict=True)
        if actual in seen:
            continue
        require(actual == actual_tool or any(actual.parent == directory.resolve(strict=True)
                                             for directory in directories), 'Host library escaped fixed directories')
        seen.add(actual)
        require(len(seen) <= 32, 'Unexpected host dependency count')
        details = elf_dependencies(actual)
        bind(logical, details['sha256'])
        state['host_elf_files'][str(actual)] = details
        if details['interpreter'] is not None:
            require(details['interpreter'] == '/lib64/ld-linux-x86-64.so.2', 'Unexpected ELF interpreter')
            pending.append(pathlib.Path(details['interpreter']))
        for needed in details['needed']:
            matches = {candidate.resolve(strict=True) for directory in directories
                       for candidate in (directory / needed,) if candidate.is_file()}
            require(len(matches) == 1, 'Missing or ambiguous host library: ' + needed)
            pending.append(next(iter(matches)))
    state['host_pkg_config_path'] = str(tool)
    state['host_pkg_config_actual_path'] = str(actual_tool)
    state['host_pkg_config_sha256'] = state['host_elf_files'][str(actual_tool)]['sha256']
    require(all(sha(path) == digest for path, digest in state['inputs'].items()), 'Bound input changed')
    require(all(str(pathlib.Path(path).resolve(strict=True)) == actual
                for path, actual in state['aliases'].items()), 'Bound alias changed')
    state.update(audit_complete=True, bound_inputs_unchanged=True, input_aliases_unchanged=True,
                 cached_scope_has_i386=False, cached_scope_has_amd64_nss=True,
                 query_still_requires_review=True,
                 scope='Authenticated selected sysroot metadata/headers and read-only host tool dependency inventory; no query, GN, runtime, TLS or certificate verification qualification')
    (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
    print(json.dumps({'audit_complete': True, 'archives': len(state['sysroots']),
                      'host_elf_files': len(state['host_elf_files']),
                      'host_pkg_config_sha256': state['host_pkg_config_sha256']}, indent=2), flush=True)
except BaseException as error:
    if authorized:
        state.update(failure=str(error), traceback=traceback.format_exc())
        (OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
    traceback.print_exc()
    sys.exit(1)

#!/usr/bin/env python3
"""Describe ELF browser/runtime requirements without executing the input.

This is an artifact inventory, not GTOS's ELF loader or an acceptance policy.
Only the guest loader and guest conformance tests can establish loadability.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MAX_FILE_BYTES = 256 * 1024 * 1024
PT_NAMES = {0: 'NULL', 1: 'LOAD', 2: 'DYNAMIC', 3: 'INTERP', 4: 'NOTE',
            6: 'PHDR', 7: 'TLS', 0x6474e550: 'GNU_EH_FRAME',
            0x6474e551: 'GNU_STACK', 0x6474e552: 'GNU_RELRO'}


def inventory(data):
    if len(data) < 16 or data[:4] != b'\x7fELF':
        raise ValueError('not an ELF file')
    elf_class, encoding, version = data[4:7]
    if elf_class not in (1, 2) or encoding != 1 or version != 1:
        raise ValueError('inventory supports ELF32/ELF64 little-endian version 1')
    header = struct.Struct('<HHIIIIIHHHHHH' if elf_class == 1 else '<HHIQQQIHHHHHH')
    if len(data) < 16 + header.size:
        raise ValueError('truncated ELF header')
    values = header.unpack_from(data, 16)
    kind, machine, elf_version, entry, phoff, shoff, flags, ehsize, phentsize, phnum = values[:10]
    if elf_version != 1 or ehsize != 16 + header.size:
        raise ValueError('invalid ELF header version or size')
    if phnum == 0xffff:
        raise ValueError('extended program-header counts are not supported by this inventory')
    ph = struct.Struct('<IIIIIIII' if elf_class == 1 else '<IIQQQQQQ')
    if phnum and phentsize != ph.size:
        raise ValueError('invalid program-header entry size')
    if phoff > len(data) or phnum * phentsize > len(data) - phoff:
        raise ValueError('truncated program-header table')
    segments, interpreters, requirements = [], [], set()
    for index in range(phnum):
        fields = ph.unpack_from(data, phoff + index * phentsize)
        if elf_class == 1:
            ptype, offset, address, physical, filesz, memsz, perm, align = fields
        else:
            ptype, perm, offset, address, physical, filesz, memsz, align = fields
        if offset > len(data) or filesz > len(data) - offset:
            raise ValueError('segment %d exceeds file bounds' % index)
        permissions = ''.join(name for bit, name in ((4, 'R'), (2, 'W'), (1, 'X')) if perm & bit)
        segments.append(dict(index=index, type=PT_NAMES.get(ptype, hex(ptype)),
                             offset=offset, address=address, file_bytes=filesz,
                             memory_bytes=memsz, permissions=permissions,
                             alignment=align))
        if ptype == 3:
            raw = data[offset:offset + filesz]
            if not raw or raw[-1] != 0 or b'\x00' in raw[:-1]:
                raise ValueError('invalid interpreter string')
            interpreters.append(raw[:-1].decode('utf-8', errors='replace'))
            requirements.add('userspace dynamic interpreter')
        if ptype == 2:
            requirements.add('dynamic-link metadata and relocations')
        if ptype == 7:
            requirements.add('thread-local-storage image and thread pointer')
        if ptype == 1 and memsz > filesz:
            requirements.add('zero-filled memory beyond file-backed bytes')
        if ptype == 1 and perm & 3 == 3:
            requirements.add('writable executable load segment: review W^X')
        if ptype == 0x6474e551 and perm & 1:
            requirements.add('executable stack: review W^X')
    if kind == 3:
        requirements.add('position-independent image/load-bias handling')
    loads = [s for s in segments if s['type'] == 'LOAD']
    return dict(schema_version=1, elf_bits=32 if elf_class == 1 else 64,
                machine={3: 'i386', 62: 'x86_64'}.get(machine, str(machine)),
                elf_type={1: 'REL', 2: 'EXEC', 3: 'DYN'}.get(kind, str(kind)),
                entry=entry, flags=flags, file_bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(), interpreters=interpreters,
                load_file_bytes=sum(s['file_bytes'] for s in loads),
                load_memory_bytes=sum(s['memory_bytes'] for s in loads),
                load_address_span=(max(s['address'] + s['memory_bytes'] for s in loads)
                                   - min(s['address'] for s in loads)) if loads else 0,
                requirements=sorted(requirements), segments=segments,
                guest_loadability='not assessed',
                scope='ELF metadata only; syscall, libc, IPC, JIT, sandbox and runtime behavior need guest tests')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        if args.artifact.stat().st_size > MAX_FILE_BYTES:
            raise ValueError('artifact exceeds 256 MiB inventory limit')
        report = inventory(args.artifact.read_bytes())
        report['artifact'] = str(args.artifact)
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(2, 'inventory error: %s\n' % exc)
    text = json.dumps(report, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    else:
        print(text, end='')


if __name__ == '__main__':
    main()

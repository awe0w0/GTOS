#!/usr/bin/env python3
"""Unit tests for the candidate provider comparison in tools/build-native-c-file-probe.py.

The comparison is what lets a rebuild from a differently-located checkout be called identical to the qualified object. These tests drive it with
synthetic 32-bit objects: an exact copy must pass without substitutions, a copy that only moves the recorded source root must pass and name the
section, a changed instruction byte must fail, and a reference whose bytes do not match the recorded identity must be refused rather than
trusted.
"""
import hashlib
import importlib.util
import pathlib
import struct
import tempfile

BUILDER = pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'build-native-c-file-probe.py'
spec = importlib.util.spec_from_file_location('c_file_build', BUILDER)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def elf32(sections):
    """Assemble a minimal little-endian ELF32 relocatable with the given (name, body) sections."""
    shstr = b'\0'
    entries = []
    for name, body in sections:
        entries.append((len(shstr), name.encode(), body))
        shstr += name.encode() + b'\0'
    # section header string table is appended last
    names_index = len(shstr)
    shstr += b'.shstrtab\0'
    header = 0x34
    payload = b''
    headers = []
    offset = header
    for name_at, name, body in entries:
        headers.append((name_at, 1, offset, len(body)))
        payload += body + b'\0' * ((4 - len(body) % 4) % 4)
        offset = header + len(payload)
    shstr_offset = header + len(payload)
    headers.append((names_index, 3, shstr_offset, len(shstr)))
    payload += shstr
    align = 4
    while (header + len(payload)) % align:
        payload += b'\0'
    shoff = header + len(payload)
    table = b''
    for name_at, kind, sec_offset, size in headers:
        table += struct.pack('<IIIIIIIIII', name_at, kind, 0, 0, sec_offset, size, 0, 0, 1, 0)
    ident = b'\x7fELF\x01\x01\x01' + b'\0' * 9
    head = ident + struct.pack('<HHIIIIIHHHHHH', 1, 3, 1, 0, 0, shoff, 0, 0x34, 0, 0, 0x28, len(headers), len(headers) - 1)
    return head + payload + table


RECORDED = b'/mnt/f/GTOS-Chromium/sources/GTOS/apps/native_c_string/llvm-libc/src/string/memset.cpp'
LOCAL = b'/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154/apps/native_c_string/llvm-libc/src/string/memset.cpp'
ROOTS = ('/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154',
         '/mnt/f/GTOS-Chromium/sources/GTOS')
failures = []


def check(name, condition, detail=''):
    print(('PASS ' if condition else 'FAIL ') + name + ((' ' + detail) if detail and not condition else ''))
    if not condition:
        failures.append(name)


with tempfile.TemporaryDirectory() as workspace:
    root = pathlib.Path(workspace)
    code = b'\x55\x89\xe5\x83\xec\x0c\x8b\x44\x24\x18\xc3'
    reference = elf32([('.text', code), ('.rodata.str1.1', RECORDED + b'\0' + b"'size_t' (aka 'unsigned int')\0")])
    qualified = root / 'qualified.o'
    qualified.write_bytes(reference)
    want = hashlib.sha256(reference).hexdigest()

    same = root / 'same.o'
    same.write_bytes(reference)
    ok, replaced = builder.provider_identity(same, qualified, want, ROOTS)
    check('identical object passes with no substitution', ok and replaced == [])

    moved = root / 'moved.o'
    moved.write_bytes(elf32([('.text', code), ('.rodata.str1.1', LOCAL + b'\0' + b"'size_t' (aka 'unsigned int')\0")]))
    ok, replaced = builder.provider_identity(moved, qualified, want, ROOTS)
    check('source-root substitution passes and names the section', ok and replaced == ['.rodata.str1.1'], str(replaced))

    changed = root / 'changed.o'
    changed.write_bytes(elf32([('.text', code[:-1] + b'\xc2'), ('.rodata.str1.1', LOCAL + b'\0' + b"'size_t' (aka 'unsigned int')\0")]))
    ok, replaced = builder.provider_identity(changed, qualified, want, ROOTS)
    check('a changed instruction byte fails', ok is False and replaced == [])

    changed = root / 'changed.o'
    changed.write_bytes(elf32([('.text', code[:-1] + b'\xc2'), ('.rodata.str1.1', RECORDED)]))
    try:
        builder.provider_identity(changed, qualified, '0' * 64, ROOTS)
        refused = False
        detail = 'the mismatched reference was accepted'
    except AssertionError as error:
        refused = 'does not match the recorded identity' in str(error)
        detail = str(error)
    check('a reference that contradicts the recorded identity is refused', refused, detail)

    extra = root / 'extra.o'
    extra.write_bytes(elf32([('.text', code), ('.note.new', b'\0' * 8), ('.rodata.str1.1', RECORDED)]))
    try:
        builder.provider_identity(extra, qualified, want, ROOTS)
        refused = False
        detail = 'the changed section set was accepted'
    except AssertionError as error:
        refused = 'Section set changed' in str(error)
        detail = str(error)
    check('a changed section set is refused', refused, detail)

    elsewhere = root / 'elsewhere.o'
    elsewhere.write_bytes(elf32([('.text', code), ('.rodata.str1.1', b'/somewhere/else/memset.cpp' + b'\0')]))
    ok, replaced = builder.provider_identity(elsewhere, qualified, want, ROOTS)
    check('a substitution outside the recorded root fails', ok is False and replaced == [])

print('FAILURES ' + str(len(failures)))
raise SystemExit(1 if failures else 0)

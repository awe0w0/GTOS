#!/usr/bin/env python3
"""Unit tests for the candidate object comparison in tools/build-native-c-file-probe.py.

The comparison is what lets a rebuild from a differently-located checkout be called identical to the qualified object, so its refusals matter as
much as its acceptance. These tests drive it with synthetic 32-bit relocatable objects:

  * an exact copy must pass with no substitutions;
  * a copy whose only difference is the recorded source root must pass and name the section;
  * a changed instruction byte must fail;
  * a reference that contradicts the recorded identity must be refused before anything is compared;
  * a different section set must be refused;
  * a substitution outside the recorded root must fail;
  * a changed ELF header field (the ABI flags) must be refused;
  * a changed section flag must be refused;
  * a changed NOBITS section size must be refused, since those sections have no content to compare.

Nothing here claims anything about a kernel, an interrupt or a device: it is a test of one function's decisions.
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

SHT_NOBITS = 8
SHT_PROGBITS = 1


def elf32(sections, abi_flags=0):
    """Assemble a minimal little-endian ELF32 relocatable.

    `sections` is a list of (name, kind, flags, body); for NOBITS the body is an int, its size.
    """
    shstr = b'\0'
    entries = []
    for name, kind, flags, body in sections:
        entries.append((len(shstr), name.encode(), kind, flags, body))
        shstr += name.encode() + b'\0'
    names_index = len(shstr)
    shstr += b'.shstrtab\0'
    header = 0x34
    payload = b''
    headers = []
    offset = header
    for name_at, name, kind, flags, body in entries:
        if kind == SHT_NOBITS:
            headers.append((name_at, kind, flags, 0, body))
        else:
            headers.append((name_at, kind, flags, offset, len(body)))
            payload += body + b'\0' * ((4 - len(body) % 4) % 4)
            offset = header + len(payload)
    shstr_offset = header + len(payload)
    headers.append((names_index, 3, 0, shstr_offset, len(shstr)))
    payload += shstr
    while (header + len(payload)) % 4:
        payload += b'\0'
    shoff = header + len(payload)
    table = b''
    for name_at, kind, flags, sec_offset, size in headers:
        table += struct.pack('<IIIIIIIIII', name_at, kind, flags, 0, sec_offset, size, 0, 0, 1, 0)
    ident = b'\x7fELF\x01\x01\x01' + b'\0' * 9
    head = ident + struct.pack('<HHIIIIIHHHHHH', 1, 3, 1, 0, 0, shoff, abi_flags, 0x34, 0, 0, 0x28, len(headers), len(headers) - 1)
    return head + payload + table


RECORDED = b'/mnt/f/GTOS-Chromium/sources/GTOS/apps/native_c_string/llvm-libc/src/string/memset.cpp'
LOCAL = (b'/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154'
         b'/apps/native_c_string/llvm-libc/src/string/memset.cpp')
ROOTS = ('/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154',
         '/mnt/f/GTOS-Chromium/sources/GTOS')
CODE = b'\x55\x89\xe5\x83\xec\x0c\x8b\x44\x24\x18\xc3'
TYPEINFO = b"'size_t' (aka 'unsigned int')\0"
failures = []


def check(name, condition, detail=''):
    print(('PASS ' if condition else 'FAIL ') + name + ((' ' + detail) if detail and not condition else ''))
    if not condition:
        failures.append(name)


def refuses(name, call, expected):
    try:
        call()
        check(name, False, 'the change was accepted')
    except AssertionError as error:
        check(name, expected in str(error), str(error))


with tempfile.TemporaryDirectory() as workspace:
    root = pathlib.Path(workspace)
    reference = elf32([('.text', SHT_PROGBITS, 0x6, CODE), ('.rodata.str1.1', SHT_PROGBITS, 0x2, RECORDED + b'\0' + TYPEINFO),
                       ('.bss', SHT_NOBITS, 0x3, 16)])
    qualified = root / 'qualified.o'
    qualified.write_bytes(reference)
    want = hashlib.sha256(reference).hexdigest()

    same = root / 'same.o'
    same.write_bytes(reference)
    ok, replaced = builder.provider_identity(same, qualified, want, ROOTS)
    check('identical object passes with no substitution', ok and replaced == [])

    moved = root / 'moved.o'
    moved.write_bytes(elf32([('.text', SHT_PROGBITS, 0x6, CODE), ('.rodata.str1.1', SHT_PROGBITS, 0x2, LOCAL + b'\0' + TYPEINFO),
                             ('.bss', SHT_NOBITS, 0x3, 16)]))
    ok, replaced = builder.provider_identity(moved, qualified, want, ROOTS)
    check('source-root substitution passes and names the section', ok and replaced == ['.rodata.str1.1'], str(replaced))

    changed = root / 'changed.o'
    changed.write_bytes(elf32([('.text', SHT_PROGBITS, 0x6, CODE[:-1] + b'\xc2'), ('.rodata.str1.1', SHT_PROGBITS, 0x2, RECORDED + b'\0' + TYPEINFO),
                               ('.bss', SHT_NOBITS, 0x3, 16)]))
    ok, replaced = builder.provider_identity(changed, qualified, want, ROOTS)
    check('a changed instruction byte fails', ok is False and replaced == [])

    refuses('a reference that contradicts the recorded identity is refused',
            lambda: builder.provider_identity(changed, qualified, '0' * 64, ROOTS),
            'does not match the recorded identity')

    extra = root / 'extra.o'
    extra.write_bytes(elf32([('.text', SHT_PROGBITS, 0x6, CODE), ('.note.new', SHT_PROGBITS, 0x2, b'\0' * 8),
                             ('.rodata.str1.1', SHT_PROGBITS, 0x2, RECORDED + b'\0' + TYPEINFO), ('.bss', SHT_NOBITS, 0x3, 16)]))
    # Adding a section changes the header's section count and string-table index, so the header check reports it first.
    refuses('a changed section set is refused', lambda: builder.provider_identity(extra, qualified, want, ROOTS), 'ELF header changed')

    elsewhere = root / 'elsewhere.o'
    elsewhere.write_bytes(elf32([('.text', SHT_PROGBITS, 0x6, CODE), ('.rodata.str1.1', SHT_PROGBITS, 0x2, b'/somewhere/else/memset.cpp\0'),
                                 ('.bss', SHT_NOBITS, 0x3, 16)]))
    ok, replaced = builder.provider_identity(elsewhere, qualified, want, ROOTS)
    check('a substitution outside the recorded root fails', ok is False and replaced == [])

    abi = root / 'abi.o'
    abi.write_bytes(elf32([('.text', SHT_PROGBITS, 0x6, CODE), ('.rodata.str1.1', SHT_PROGBITS, 0x2, RECORDED + b'\0' + TYPEINFO),
                           ('.bss', SHT_NOBITS, 0x3, 16)], abi_flags=0x10))
    refuses('changed ELF header flags are refused', lambda: builder.provider_identity(abi, qualified, want, ROOTS), 'ELF header changed')

    flagged = root / 'flagged.o'
    flagged.write_bytes(elf32([('.text', SHT_PROGBITS, 0x7, CODE), ('.rodata.str1.1', SHT_PROGBITS, 0x2, RECORDED + b'\0' + TYPEINFO),
                               ('.bss', SHT_NOBITS, 0x3, 16)]))
    refuses('changed section flags are refused', lambda: builder.provider_identity(flagged, qualified, want, ROOTS), 'Section flags changed')

    grown = root / 'grown.o'
    grown.write_bytes(elf32([('.text', SHT_PROGBITS, 0x6, CODE), ('.rodata.str1.1', SHT_PROGBITS, 0x2, RECORDED + b'\0' + TYPEINFO),
                             ('.bss', SHT_NOBITS, 0x3, 32)]))
    refuses('a changed NOBITS section size is refused', lambda: builder.provider_identity(grown, qualified, want, ROOTS),
            'NOBITS section size changed')

print('FAILURES ' + str(len(failures)))
raise SystemExit(1 if failures else 0)

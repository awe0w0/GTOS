#!/usr/bin/env python3
"""Audit linked x86 instruction streams, excluding data merged into .text.

A GNU ld map identifies each input section's final address. ELF SHF_EXECINSTR
selects code, rather than the output section name: the kernel linker merges
rodata and Multiboot records into .text. The copied AP trampoline is the one
explicit non-SHF_EXECINSTR code range, decoded in its actual 16/32-bit modes.
No source directory, object, or entire output section receives an FP exemption.
"""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

HELPERS = frozenset(("native_fp_save_asm", "native_fp_neutral_asm",
                     "native_fp_probe_asm", "native_fp_restore_asm"))


class AuditError(Exception):
    pass


class Elf:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        if self.data[:4] != b"\x7fELF" or self.data[5] != 1:
            raise AuditError(f"{path}: expected little-endian ELF")
        self.bits = {1: 32, 2: 64}.get(self.data[4])
        if not self.bits:
            raise AuditError(f"{path}: unsupported ELF class")
        h = struct.unpack_from("<HHIIIIIHHHHHH" if self.bits == 32 else
                               "<HHIQQQIHHHHHH", self.data, 16)
        self.kind = h[0]
        if h[1] not in (3, 62):
            raise AuditError(f"{path}: expected x86 ELF")
        if h[11] == 0 or h[12] >= h[11]:
            raise AuditError(f"{path}: missing/extended section table unsupported")
        self.sections = []
        for n in range(h[11]):
            v = struct.unpack_from("<IIIIIIIIII" if self.bits == 32 else
                                   "<IIQQQQIIQQ", self.data, h[5] + n * h[10])
            self.sections.append(dict(zip(("name_offset", "type", "flags", "addr",
                                           "offset", "size", "link", "info",
                                           "align", "entsize"), v)))
        names = self.section_data(self.sections[h[12]])
        for section in self.sections:
            section["name"] = self.string(names, section["name_offset"])
        self.symbols = {}
        for section in self.sections:
            if section["type"] != 2:  # SHT_SYMTAB; stripped kernels fail closed.
                continue
            strings = self.section_data(self.sections[section["link"]])
            for off in range(section["offset"], section["offset"] + section["size"],
                             section["entsize"]):
                v = struct.unpack_from("<IIIBBH" if self.bits == 32 else "<IBBHQQ",
                                       self.data, off)
                if self.bits == 32:
                    name, value, size, info, _, index = v
                else:
                    name, info, _, index, value, size = v
                name = self.string(strings, name)
                if name and index:
                    self.symbols[name] = dict(value=value, size=size, type=info & 15,
                                              section=index)
        if not self.symbols:
            raise AuditError(f"{path}: symbol table required for bounded audit")

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(b"\0", offset)].decode("utf-8")

    def section_data(self, section):
        return self.data[section["offset"]:section["offset"] + section["size"]]

    def containing(self, address, size):
        for section in self.sections:
            if section["type"] != 8 and section["addr"] <= address and \
                    address + size <= section["addr"] + section["size"]:
                return section
        raise AuditError(f"{self.path}: unmapped code range {address:#x}+{size:#x}")


def code_ranges(binary, map_path):
    text = Path(map_path).read_text()
    if "Linker script and memory map" not in text:
        raise AuditError("expected a complete GNU ld map")
    discarded_text = text.split("Discarded input sections", 1)[1].split("Memory Configuration", 1)[0] if "Discarded input sections" in text else ""
    text = text.split("Linker script and memory map", 1)[1]
    inputs = {}
    for line in text.splitlines():
        if line.startswith("LOAD "):
            path = line[5:].strip()
            if path.endswith(".a"):
                raise AuditError("archive inputs need explicit audit support; do not skip them")
            obj = Elf(path)
            if obj.kind != 1:
                raise AuditError(f"{path}: input must be relocatable ELF")
            inputs[path] = obj
    if not inputs:
        raise AuditError("link map has no input objects")
    # ld wraps long section names onto their own line.
    section = None
    placements = {}
    discarded = set()
    full = re.compile(r"^ (\.[^\s]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?)\s*$")
    continuation = re.compile(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?)\s*$")
    for is_discarded, line in [(True, line) for line in discarded_text.splitlines()] + [(False, line) for line in text.splitlines()]:
        match = full.match(line)
        if match:
            section, address, size, path = match.groups()
        elif re.match(r"^ \.[^\s]+\s*$", line):
            section = line.strip()
            continue
        else:
            match = continuation.match(line) if section else None
            if not match:
                section = None
                continue
            address, size, path = match.groups()
        if path in inputs:
            key = (path, section)
            if is_discarded:
                discarded.add(key)
                section = None
                continue
            if key in placements:
                raise AuditError(f"ambiguous mapped input section: {key}")
            placements[key] = (int(address, 16), int(size, 16))
        section = None
    ranges = []
    for path, obj in inputs.items():
        for section in obj.sections:
            if not section["flags"] & 4 or not section["size"]:  # SHF_EXECINSTR
                continue
            key = (path, section["name"])
            if key not in placements and key in discarded:
                continue  # Exact discarded COMDAT section; no linked code remains.
            if key not in placements:
                raise AuditError(f"executable input section absent from map: {key}")
            address, size = placements[key]
            if size != section["size"]:
                raise AuditError(f"executable input section size changed: {key}")
            binary.containing(address, size)
            ranges.append((address, address + size, binary.bits))
    trampoline = ("gtos_ap_trampoline_start", "gtos_ap_trampoline_protected",
                  "gtos_ap_trampoline_gdt")
    present = [name in binary.symbols for name in trampoline]
    has_trampoline = any(section["name"] == ".rodata.ap_trampoline" and section["size"]
                         for obj in inputs.values() for section in obj.sections)
    if any(present) or has_trampoline:
        if not all(present):
            raise AuditError("incomplete AP trampoline boundaries")
        start, protected, end = [binary.symbols[name]["value"] for name in trampoline]
        if not start < protected < end:
            raise AuditError("invalid AP trampoline code boundaries")
        ranges.extend(((start, protected, 16), (protected, end, 32)))
    if not ranges:
        raise AuditError("no executable input code found")
    # Adjacent input sections can be decoded together; gaps/data cannot.
    merged = []
    for start, end, bits in sorted(ranges):
        if merged and start < merged[-1][1]:
            raise AuditError("overlapping executable ranges")
        if merged and start == merged[-1][1] and bits == merged[-1][2]:
            merged[-1] = (merged[-1][0], end, bits)
        else:
            merged.append((start, end, bits))
    return merged, len(inputs)


def allowed_ranges(binary, user_symbols, user_ranges, code):
    allowed = []
    for name in sorted(HELPERS | set(user_symbols)):
        symbol = binary.symbols.get(name)
        if symbol is None:
            if name in user_symbols:
                raise AuditError(f"missing explicitly allowed user symbol: {name}")
            continue
        if symbol["type"] != 2 or not symbol["size"]:
            raise AuditError(f"{name}: whitelist requires a sized STT_FUNC symbol")
        allowed.append((symbol["value"], symbol["value"] + symbol["size"], name))
    for pair in user_ranges:
        parts = pair.split(":")
        if len(parts) != 2 or any(part not in binary.symbols for part in parts):
            raise AuditError(f"invalid explicitly allowed user range: {pair}")
        start, end = [binary.symbols[name]["value"] for name in parts]
        allowed.append((start, end, pair))
    for start, end, name in allowed:
        if start >= end or not any(a <= start < end <= b for a, b, _ in code):
            raise AuditError(f"{name}: allowance is not bounded within executable code")
    return allowed


def touches_fp(instruction):
    # Remove printed branch target names/comments before matching registers.
    operands = instruction.split("#", 1)[0].split("<", 1)[0]
    words = operands.lower().split()
    while words and (words[0] in ("data16", "addr16", "addr32", "lock", "rep",
                                  "repe", "repz", "repne", "repnz", "bnd",
                                  "cs", "ds", "es", "fs", "gs", "ss") or
                     words[0].startswith("rex")):
        words.pop(0)
    if not words:
        return False
    mnemonic = words[0]
    return (bool(re.search(r"%(?:[xyz]mm\d+|mm[0-7]|k[0-7]|st(?:\(\d\))?)\b", operands))
            or mnemonic.startswith("f")  # all x87, FXSAVE/FXRSTOR, FEMMS
            or mnemonic in ("wait", "emms", "ldmxcsr", "stmxcsr")
            or mnemonic.startswith(("xsave", "xrstor", "vzero", "ldtilecfg",
                                     "sttilecfg", "tile"))
            or (mnemonic.startswith("v") and mnemonic not in ("verr", "verw")))


def audit_sources(root):
    # FP ABI signatures can silently turn into soft-float library calls under
    # -msoft-float. Disallow those interfaces/intrinsics even if unreferenced.
    ignored = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    forbidden = re.compile(r"\b(?:float|double|_Float\d+(?:x)?|__float128|__m(?:64|128|256|512)(?:i|d)?|_mm\w*|__builtin_ia32_\w*)\b")
    count = 0
    for directory in ("include", "src"):
        for path in sorted((Path(root) / directory).rglob("*")):
            if path.suffix not in (".c", ".cc", ".cpp", ".h", ".hpp"):
                continue
            code = ignored.sub(lambda match: "\n" * match.group(0).count("\n"), path.read_text())
            match = forbidden.search(code)
            if match:
                line = code.count("\n", 0, match.start()) + 1
                raise AuditError(f"{path}:{line}: prohibited kernel FP interface/intrinsic: {match.group()}")
            count += 1
    if not count:
        raise AuditError("kernel source check found no translation units or headers")
    print(f"PASS integer-only kernel interfaces: {count} source/header files")


def audit(args):
    if args.source_root:
        audit_sources(args.source_root)
    binary = Elf(args.binary)
    if binary.kind != 2:
        raise AuditError("audit expects the final ET_EXEC linked kernel")
    code, objects = code_ranges(binary, args.map)
    allowed = allowed_ranges(binary, args.allow_user_symbol + args.allow_test_kernel_symbol, args.allow_user_range, code)
    failures = []
    instructions = 0
    allowed_count = 0
    for start, end, bits in code:
        section = binary.containing(start, end - start)
        arch = {16: "i8086", 32: "i386", 64: "i386:x86-64"}[bits]
        command = [args.objdump, "-D", "-z", "--no-show-raw-insn", "-m", arch,
                   "--section=" + section["name"], f"--start-address={start}",
                   f"--stop-address={end}", str(binary.path)]
        output = subprocess.check_output(command, text=True, env=dict(os.environ, LC_ALL="C"))
        found = 0
        for line in output.splitlines():
            match = re.match(r"^\s*([0-9a-f]+):\s+(.+)$", line)
            if not match:
                continue
            address = int(match[1], 16)
            instruction = match[2].strip()
            instructions += 1
            found += 1
            if "(bad)" in instruction or instruction.startswith(".byte"):
                failures.append(f"{address:#x}: undecodable instruction: {instruction}")
            elif touches_fp(instruction):
                exemption = next((name for a, b, name in allowed if a <= address < b), None)
                if exemption:
                    allowed_count += 1
                else:
                    failures.append(f"{address:#x}: prohibited FP/SIMD: {instruction}")
        if not found:
            raise AuditError(f"objdump produced no instructions for {start:#x}:{end:#x}")
    if failures:
        raise AuditError("\n".join(failures))
    print(f"PASS integer-only linked code: {binary.path}: {objects} input objects, "
          f"{instructions} instructions, {allowed_count} FP instructions inside exact allowances")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--source-root", help="also forbid FP types/intrinsics in kernel src/ and include/")
    parser.add_argument("--map", required=True, help="GNU ld -Map output for this exact link")
    parser.add_argument("--objdump", default=os.environ.get("OBJDUMP", "objdump"))
    parser.add_argument("--allow-user-symbol", action="append", default=[], metavar="NAME",
                        help="exact sized function containing an intentional CPL3 payload")
    parser.add_argument("--allow-test-kernel-symbol", action="append", default=[], metavar="NAME",
                        help="exact sized CPL0 function in a separately built test-only kernel; never production")
    parser.add_argument("--allow-user-range", action="append", default=[], metavar="START:END",
                        help="explicit start/end symbols of an intentional CPL3 payload")
    args = parser.parse_args()
    try:
        audit(args)
    except (AuditError, OSError, ValueError, struct.error, subprocess.CalledProcessError) as error:
        print(f"FAIL kernel instruction audit: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

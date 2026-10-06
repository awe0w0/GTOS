#!/usr/bin/env python3
"""Assemble and inspect bounded GTOS bytecode packages (Python stdlib only)."""

import argparse
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import zlib

MAGIC = b"GTAPP01\0"
VERSION = 1
HEADER_SIZE = 128
MAX_PACKAGE_SIZE = 8192
INSTRUCTION_SIZE = 8
MAX_INSTRUCTIONS = (MAX_PACKAGE_SIZE - HEADER_SIZE) // INSTRUCTION_SIZE
REGISTER_COUNT = 32
INSTRUCTION = struct.Struct("<BBBBi")
ID_RE = re.compile(r"[a-z0-9_-]{1,23}\Z")
LABEL_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
REGISTER_RE = re.compile(r"r([0-9]+)\Z", re.IGNORECASE)
# Operand types: r register, i signed immediate, j instruction label/index,
# s input selector, t text selector, p positive random bound, k color index.
SPECS = {
    "halt": (0, ""), "movi": (1, "ri"), "mov": (2, "rr"),
    "add": (3, "rrr"), "sub": (4, "rrr"), "mul": (5, "rrr"),
    "mod": (6, "rrr"), "and": (7, "rrr"), "lt": (8, "rrr"),
    "eq": (9, "rrr"), "jmp": (10, "j"), "jnz": (11, "rj"),
    "input": (12, "rs"), "random": (13, "rp"), "clear": (14, "k"),
    "rect": (15, "rrrrk"), "text": (16, "rrt"),
    "number": (17, "rrrk"), "yield": (18, ""),
}
NAMES = {value[0]: key for key, value in SPECS.items()}


class PackageError(ValueError):
    """Invalid assembly or package; safe to report without a traceback."""


def integer(value, what, minimum=-(1 << 31), maximum=(1 << 31) - 1):
    if isinstance(value, bool) or not isinstance(value, int):
        raise PackageError("%s must be an integer" % what)
    if not minimum <= value <= maximum:
        raise PackageError("%s must be in [%d, %d]" % (what, minimum, maximum))
    return value


def register(value):
    match = REGISTER_RE.fullmatch(value) if isinstance(value, str) else None
    if match is None:
        raise PackageError("register must be r0 through r31")
    return integer(int(match.group(1)), "register", 0, REGISTER_COUNT - 1)


def ascii_field(value, what, limit, nonempty=False):
    if not isinstance(value, str):
        raise PackageError("%s must be a string" % what)
    if len(value) > limit or (nonempty and not value):
        raise PackageError("%s must have %s%d characters" %
                           (what, "1 to " if nonempty else "at most ", limit))
    if any(not 32 <= ord(char) <= 126 for char in value):
        raise PackageError("%s must contain printable ASCII only" % what)
    return value.encode("ascii")


def instruction_target(value, labels, count):
    if isinstance(value, str):
        if value not in labels:
            raise PackageError("unknown label %r" % value)
        value = labels[value]
    return integer(value, "instruction target", 0, count - 1)


def encode_instruction(row, labels, count):
    if not isinstance(row, list) or not row or not isinstance(row[0], str):
        raise PackageError("instruction must be a nonempty JSON array")
    name = row[0].lower()
    if name not in SPECS:
        raise PackageError("unknown opcode %r" % row[0])
    opcode, types = SPECS[name]
    if len(row) - 1 != len(types):
        raise PackageError("%s expects %d operands" % (name, len(types)))
    values = []
    for kind, value in zip(types, row[1:]):
        if kind == "r":
            values.append(register(value))
        elif kind == "j":
            values.append(instruction_target(value, labels, count))
        else:
            bounds = {"i": (-(1 << 31), (1 << 31) - 1), "s": (0, 3),
                      "t": (0, 1), "p": (1, 1000000), "k": (0, 255)}[kind]
            values.append(integer(value, "%s operand" % name, *bounds))
    a = b = c = immediate = 0
    if name == "rect":
        a, b, c, height, color = values
        immediate = height | (color << 8)
    else:
        regs = [value for kind, value in zip(types, values) if kind == "r"]
        if regs:
            a = regs[0]
        if len(regs) > 1:
            b = regs[1]
        if len(regs) > 2:
            c = regs[2]
        other = [value for kind, value in zip(types, values) if kind != "r"]
        if other:
            immediate = other[0]
    return INSTRUCTION.pack(opcode, a, b, c, immediate)


def assemble(source):
    """Return a deterministic package assembled from a decoded JSON object."""
    if not isinstance(source, dict):
        raise PackageError("source must be a JSON object")
    allowed = {"id", "title", "summary", "entry", "code"}
    unknown = set(source) - allowed
    missing = {"id", "title", "summary", "code"} - set(source)
    if unknown or missing:
        raise PackageError("source fields: unknown=%s missing=%s" %
                           (sorted(unknown), sorted(missing)))
    package_id = source["id"]
    if not isinstance(package_id, str) or not ID_RE.fullmatch(package_id):
        raise PackageError("id must match [a-z0-9_-] and have 1 to 23 characters")
    title = ascii_field(source["title"], "title", 23, nonempty=True)
    summary = ascii_field(source["summary"], "summary", 39)
    code = source["code"]
    if not isinstance(code, list):
        raise PackageError("code must be a JSON array")
    labels, instructions = {}, []
    for index, row in enumerate(code):
        if isinstance(row, dict):
            if set(row) == {"comment"} and isinstance(row["comment"], str):
                continue
            if set(row) != {"label"} or not isinstance(row["label"], str):
                raise PackageError("code item %d must be a label or comment" % index)
            label = row["label"]
            if not LABEL_RE.fullmatch(label):
                raise PackageError("invalid label %r" % label)
            if label in labels:
                raise PackageError("duplicate label %r" % label)
            labels[label] = len(instructions)
        else:
            instructions.append(row)
    count = integer(len(instructions), "instruction count", 1, MAX_INSTRUCTIONS)
    entry = instruction_target(source.get("entry", 0), labels, count)
    encoded = []
    for index, row in enumerate(instructions):
        try:
            encoded.append(encode_instruction(row, labels, count))
        except PackageError as exc:
            raise PackageError("instruction %d: %s" % (index, exc)) from exc
    body = b"".join(encoded)
    header = bytearray(HEADER_SIZE)
    header[:8] = MAGIC
    struct.pack_into("<6I", header, 8, VERSION, HEADER_SIZE,
                     HEADER_SIZE + len(body), count, entry, 0)
    header[32:32 + len(package_id)] = package_id.encode("ascii")
    header[56:56 + len(title)] = title
    header[80:80 + len(summary)] = summary
    package = header + body
    struct.pack_into("<I", package, 120, zlib.crc32(package) & 0xFFFFFFFF)
    # Check the exact bytes that will be emitted, not just the source model.
    inspect_package(package)
    return bytes(package)


def decode_field(data, start, length, what, nonempty=False):
    raw = data[start:start + length]
    end = raw.find(b"\0")
    if end == -1 or any(raw[end:]):
        raise PackageError("%s must be NUL terminated and zero padded" % what)
    try:
        value = raw[:end].decode("ascii")
    except UnicodeDecodeError as exc:
        raise PackageError("%s must be ASCII" % what) from exc
    ascii_field(value, what, length - 1, nonempty)
    return value


def inspect_package(data):
    """Validate a package and return its metadata and decoded instructions."""
    if not isinstance(data, (bytes, bytearray)):
        raise PackageError("package must be bytes")
    if not HEADER_SIZE + INSTRUCTION_SIZE <= len(data) <= MAX_PACKAGE_SIZE:
        raise PackageError("package size is outside the supported bounds")
    if data[:8] != MAGIC:
        raise PackageError("bad package magic")
    version, header_size, total, count, entry, flags = struct.unpack_from("<6I", data, 8)
    if version != VERSION or header_size != HEADER_SIZE:
        raise PackageError("unsupported package version or header size")
    if total != len(data) or total != HEADER_SIZE + count * INSTRUCTION_SIZE:
        raise PackageError("package length does not match instruction count")
    integer(count, "instruction count", 1, MAX_INSTRUCTIONS)
    integer(entry, "entry instruction", 0, count - 1)
    if flags or struct.unpack_from("<I", data, 124)[0]:
        raise PackageError("flags and reserved fields must be zero")
    checksum = struct.unpack_from("<I", data, 120)[0]
    checksum_data = bytearray(data)
    checksum_data[120:124] = b"\0" * 4
    if zlib.crc32(checksum_data) & 0xFFFFFFFF != checksum:
        raise PackageError("CRC32 mismatch")
    package_id = decode_field(data, 32, 24, "id", True)
    if not ID_RE.fullmatch(package_id):
        raise PackageError("invalid package id")
    title = decode_field(data, 56, 24, "title", True)
    summary = decode_field(data, 80, 40, "summary")
    decoded = []
    for index in range(count):
        opcode, a, b, c, immediate = INSTRUCTION.unpack_from(data, HEADER_SIZE + index * 8)
        if opcode not in NAMES:
            raise PackageError("instruction %d: unknown opcode %d" % (index, opcode))
        name = NAMES[opcode]
        types = SPECS[name][1]
        operands = []
        regs = iter((a, b, c))
        for kind in types:
            if kind == "r":
                # RECT's fourth register is packed in the low immediate byte.
                value = next(regs, immediate & 255)
                operands.append("r%d" % value)
            elif name == "rect":
                if immediate < 0 or immediate > 65535:
                    raise PackageError("instruction %d: invalid RECT immediate" % index)
                operands.append((immediate >> 8) & 255)
            else:
                operands.append(immediate)
        row = [name] + operands
        try:
            canonical = encode_instruction(row, {}, count)
        except PackageError as exc:
            raise PackageError("instruction %d: %s" % (index, exc)) from exc
        # Unused operand bytes are reserved. Keep packages reproducible and
        # leave these bytes available for a future, explicitly versioned ABI.
        if canonical != data[HEADER_SIZE + index * 8:HEADER_SIZE + (index + 1) * 8]:
            raise PackageError("instruction %d: unused operand fields must be zero" % index)
        decoded.append(row)
    return {"id": package_id, "title": title, "summary": summary,
            "version": version, "bytes": total, "instructions": count,
            "entry": entry, "crc32": "%08x" % checksum, "code": decoded}


def no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise PackageError("duplicate JSON key %r" % key)
        result[key] = value
    return result


def load_source(path):
    with Path(path).open("r", encoding="utf-8") as stream:
        return json.load(stream, object_pairs_hook=no_duplicate_keys)


def write_package(path, data):
    """Atomically replace the output only after a complete successful build."""
    output = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=output.parent,
                                         prefix=".%s." % output.name, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build", help="assemble JSON source into .gtapp")
    build.add_argument("source", type=Path)
    build.add_argument("output", type=Path)
    inspect = subparsers.add_parser("inspect", help="validate a .gtapp and print metadata")
    inspect.add_argument("package", type=Path)
    inspect.add_argument("--disassemble", action="store_true", help="include decoded instructions")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            if args.source.resolve() == args.output.resolve():
                raise PackageError("source and output must be different files")
            data = assemble(load_source(args.source))
            write_package(args.output, data)
            metadata = inspect_package(data)
            print("Built %s: %s, %d instructions, %d bytes, CRC32 %s" %
                  (args.output, metadata["id"], metadata["instructions"],
                   metadata["bytes"], metadata["crc32"]))
        else:
            # A bounded read rejects giant/corrupt files without loading them.
            with args.package.open("rb") as stream:
                data = stream.read(MAX_PACKAGE_SIZE + 1)
            metadata = inspect_package(data)
            if not args.disassemble:
                del metadata["code"]
            print(json.dumps(metadata, indent=2))
    except (PackageError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        parser.exit(1, "package: %s\n" % exc)
    return 0


if __name__ == "__main__":
    sys.exit(main())

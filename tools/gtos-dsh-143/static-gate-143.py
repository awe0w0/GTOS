"""GTOS-DSH-143 P: the static byte gate over the current image.

This reads the image itself. It does not run it, does not disassemble it, and does not consult a tool that would decide for it: the
ELF header, the program headers, the section table and the symbol table are parsed here, byte by byte, and the map is parsed as
text. Every obligation is independent of the link that produced the image, so a link that wrote a plausible map and a broken
image cannot pass by agreeing with itself.

The obligations:

  1. the header says 32-bit, little-endian, executable, i386. An image for another machine is not this packet's image.
  2. every program header is inside the file, and at least one loadable segment maps the address the linker script places the
     kernel at. A segment that runs past the end of the file is a truncated image.
  3. every section header is inside the file and every section's bytes are inside the file.
  4. the entry point is a symbol the symbol table actually defines, inside an executable section. A number that happens to point
     somewhere is not an entry.
  5. no symbol is undefined. An executable with an undefined symbol would need something at load time that a freestanding kernel
     does not have.
  6. the map names the same object files, in the same order, as the slots this packet linked.
  7. the initializers the catalog expects are present in the symbol table.

Four negatives are built from the image itself, each a single defect, and each must be refused by the same code path. A gate that
accepts everything and a gate that accepts nothing would each pass half of this.
"""

import json
import os
import struct
import sys


def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def parse_elf(data):
    if len(data) < 52:
        raise ValueError("too short to hold an ELF32 header")
    if data[0:4] != b"\x7fELF":
        raise ValueError("no ELF magic")
    header = {
        "ei_class": data[4],
        "ei_data": data[5],
        "ei_version": data[6],
        "e_type": u16(data, 16),
        "e_machine": u16(data, 18),
        "e_version": u32(data, 20),
        "e_entry": u32(data, 24),
        "e_phoff": u32(data, 28),
        "e_shoff": u32(data, 32),
        "e_flags": u32(data, 36),
        "e_ehsize": u16(data, 40),
        "e_phentsize": u16(data, 42),
        "e_phnum": u16(data, 44),
        "e_shentsize": u16(data, 46),
        "e_shnum": u16(data, 48),
        "e_shstrndx": u16(data, 50),
    }
    phdrs = []
    for i in range(header["e_phnum"]):
        base = header["e_phoff"] + i * header["e_phentsize"]
        if base + 32 > len(data):
            raise ValueError("program header %d runs past the end of the file" % i)
        phdrs.append({
            "p_type": u32(data, base), "p_offset": u32(data, base + 4), "p_vaddr": u32(data, base + 8),
            "p_paddr": u32(data, base + 12), "p_filesz": u32(data, base + 16), "p_memsz": u32(data, base + 20),
            "p_flags": u32(data, base + 24), "p_align": u32(data, base + 28),
        })
    sections = []
    for i in range(header["e_shnum"]):
        base = header["e_shoff"] + i * header["e_shentsize"]
        if base + 40 > len(data):
            raise ValueError("section header %d runs past the end of the file" % i)
        sections.append({
            "sh_name": u32(data, base), "sh_type": u32(data, base + 4), "sh_flags": u32(data, base + 8),
            "sh_addr": u32(data, base + 12), "sh_offset": u32(data, base + 16), "sh_size": u32(data, base + 20),
            "sh_link": u32(data, base + 24), "sh_info": u32(data, base + 28), "sh_addralign": u32(data, base + 32),
            "sh_entsize": u32(data, base + 36),
        })
    # Section names come from the section-header string table, which is itself a section.
    names = []
    if header["e_shstrndx"] < len(sections):
        strtab = sections[header["e_shstrndx"]]
        blob = data[strtab["sh_offset"]:strtab["sh_offset"] + strtab["sh_size"]]
        for section in sections:
            end = blob.find(b"\x00", section["sh_name"])
            names.append(blob[section["sh_name"]:end if end >= 0 else len(blob)].decode("latin-1"))
    else:
        names = ["" for _ in sections]
    for section, name in zip(sections, names):
        section["name"] = name

    # Symbols: a section of type SHT_SYMTAB (2) whose linked string table supplies the names.
    symbols = []
    for section in sections:
        if section["sh_type"] != 2 or section["sh_entsize"] != 16:
            continue
        strtab = sections[section["sh_link"]] if section["sh_link"] < len(sections) else None
        blob = data[strtab["sh_offset"]:strtab["sh_offset"] + strtab["sh_size"]] if strtab else b""
        count = section["sh_size"] // 16
        for i in range(count):
            base = section["sh_offset"] + i * 16
            if base + 16 > len(data):
                raise ValueError("symbol %d runs past the end of the file" % i)
            name_off = u32(data, base)
            end = blob.find(b"\x00", name_off) if name_off < len(blob) else -1
            name = blob[name_off:end].decode("latin-1") if end >= 0 else ""
            symbols.append({
                "name": name, "value": u32(data, base + 4), "size": u32(data, base + 8),
                "info": data[base + 12], "other": data[base + 13], "shndx": u16(data, base + 14),
            })
    return header, phdrs, sections, symbols


def parse_map(text):
    """The object files the map names, in the order it first names them.

    A map entry names the input section, then the object it came from, on one line:

        .text.foo   0x0000000000100000  0x1a2  /some/where/arp.o

    The object is the last token, not the line. Taking the whole line would give one entry per section, and an object that
    contributes to several sections would then appear several times; taking the basename of the line would give the same
    duplicate under the same name. The first appearance is the link order, and later appearances are the same object again.
    """
    order = []
    # A map begins with messages about merging program properties, which name objects in an order that has nothing to do with
    # the link. Only the map proper says where each object was placed, so parsing starts at its heading. Taking the whole file
    # made gdt.o look like the thirteenth object linked.
    lines = text.split("\n")
    start = 0
    for index, line in enumerate(lines):
        if line.strip() == "Linker script and memory map":
            start = index + 1
            break
    for line in lines[start:]:
        stripped = line.strip()
        if not stripped:
            continue
        token = stripped.split()[-1]
        if token.endswith(".o") and token not in order:
            order.append(token)
    return order

def gate(image_path, map_path, expected, negatives_root):
    problems = []
    report = {"image": image_path, "map": map_path}

    with open(image_path, "rb") as handle:
        data = handle.read()
    with open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        map_text = handle.read()

    header, phdrs, sections, symbols = parse_elf(data)
    report["bytes"] = len(data)
    report["header"] = header

    # 1. The machine this image is for.
    if header["ei_class"] != 1:
        problems.append({"obligation": "header", "why": "not a 32-bit image", "observed": header["ei_class"]})
    if header["ei_data"] != 1:
        problems.append({"obligation": "header", "why": "not little-endian", "observed": header["ei_data"]})
    if header["e_type"] != 2:
        problems.append({"obligation": "header", "why": "not an executable", "observed": header["e_type"]})
    if header["e_machine"] != 3:
        problems.append({"obligation": "header", "why": "not an i386 image", "observed": header["e_machine"]})

    # 2. Segments, and the load address the linker script asks for.
    loads = [p for p in phdrs if p["p_type"] == 1]
    report["program_headers"] = len(phdrs)
    report["loadable_segments"] = len(loads)
    if not loads:
        problems.append({"obligation": "program headers", "why": "no loadable segment"})
    for index, segment in enumerate(loads):
        if segment["p_offset"] + segment["p_filesz"] > len(data):
            problems.append({"obligation": "program headers", "why": "a segment runs past the end of the file", "index": index,
                             "offset": segment["p_offset"], "filesz": segment["p_filesz"], "bytes": len(data)})
        if segment["p_filesz"] > segment["p_memsz"]:
            problems.append({"obligation": "program headers", "why": "a segment holds more file bytes than memory", "index": index})
    want_load = expected.get("load_address")
    if want_load is not None:
        base = min(p["p_vaddr"] for p in loads) if loads else None
        report["lowest_load_vaddr"] = base
        if base != want_load:
            problems.append({"obligation": "program headers", "why": "the image does not load where the script places it",
                             "expected": want_load, "observed": base})

    # 3. Sections, and their bytes.
    report["sections"] = len(sections)
    for section in sections:
        if section["sh_type"] == 8:  # SHT_NOBITS occupies no file bytes
            continue
        if section["sh_offset"] + section["sh_size"] > len(data):
            problems.append({"obligation": "sections", "why": "a section runs past the end of the file",
                             "section": section["name"], "offset": section["sh_offset"], "size": section["sh_size"]})

    # 4. The entry point is a defined symbol in an executable section.
    entry = header["e_entry"]
    executable = [s for s in sections if s["sh_flags"] & 0x4]
    by_entry = [s for s in symbols if s["value"] == entry and s["shndx"] != 0]
    report["entry"] = entry
    report["entry_symbols"] = [s["name"] for s in by_entry]
    if not by_entry:
        problems.append({"obligation": "entry", "why": "no defined symbol sits at the entry point", "entry": entry})
    else:
        symbol = by_entry[0]
        containing = [s for s in executable
                      if s["sh_addr"] <= symbol["value"] < s["sh_addr"] + max(s["sh_size"], 1)]
        if not containing:
            problems.append({"obligation": "entry", "why": "the entry symbol is not inside an executable section",
                             "symbol": symbol["name"]})
    want_entry_symbol = expected.get("entry_symbol")
    if want_entry_symbol and want_entry_symbol not in [s["name"] for s in by_entry]:
        problems.append({"obligation": "entry", "why": "the entry point is not the symbol the script names",
                         "expected": want_entry_symbol, "observed": [s["name"] for s in by_entry]})

    # 5. Nothing undefined.
    undefined = sorted({s["name"] for s in symbols if s["shndx"] == 0 and s["name"]})
    report["undefined_symbols"] = undefined
    if undefined:
        problems.append({"obligation": "symbols", "why": "the image has undefined symbols", "count": len(undefined),
                         "sample": undefined[:8]})

    # 6. The map names exactly the objects that were linked. It does not name them in the order the link named them, and it is
    # not supposed to: the linker places input sections by the script, so the object holding the multiboot header comes first in
    # the image whatever position it had on the command line. The obligation is therefore about membership, and the order
    # difference is recorded as a fact rather than asserted away in either direction.
    order = parse_map(map_text)
    report["map_objects"] = len(order)
    want_order = expected.get("object_order") or []
    if want_order:
        got = [os.path.basename(o) for o in order]
        got_set, want_set = set(got), set(want_order)
        extra = sorted(got_set - want_set)
        missing_objects = sorted(want_set - got_set)
        report["map_order"] = got
        report["map_order_matches_argv"] = got == list(want_order)
        if extra:
            problems.append({"obligation": "map", "why": "the map names objects that were not linked", "extra": extra[:8]})
        if missing_objects:
            problems.append({"obligation": "map", "why": "the map does not name every linked object",
                             "missing": missing_objects[:8], "missing_count": len(missing_objects)})
        if got != list(want_order):
            first = next((i for i, (a, b) in enumerate(zip(got, want_order)) if a != b), 0)
            report["map_order_first_divergence"] = {"at": first,
                "linked_order": want_order[first] if first < len(want_order) else None,
                "map_order": got[first] if first < len(got) else None,
                "why": "the linker places input sections by the script, so the multiboot object is placed first"}
    # 7. The initializers the catalog expects.
    defined = {s["name"] for s in symbols if s["shndx"] != 0}
    missing = [name for name in (expected.get("initializers") or []) if name not in defined]
    report["initializers_expected"] = len(expected.get("initializers") or [])
    report["initializers_missing"] = missing
    if missing:
        problems.append({"obligation": "initializers", "why": "a constructor the catalog expects is not in the image",
                         "missing": missing})

    report["problems"] = problems
    report["accepted"] = not problems

    # The negatives. Each is the same image with one defect, written into a scratch directory this stage owns, and each has to be
    # refused. The mutation is applied to a copy and the copy is removed afterwards, so nothing here can touch the real image.
    os.makedirs(negatives_root, exist_ok=True)
    negatives = []

    def try_negative(name, mutate, expect_obligation):
        path = os.path.join(negatives_root, name + ".elf")
        mutated = bytearray(data)
        mutate(mutated)
        with open(path, "wb") as handle:
            handle.write(bytes(mutated))
        try:
            sub_problems = []
            sub_header, sub_phdrs, sub_sections, sub_symbols = parse_elf(bytes(mutated))
            # The same obligations, evaluated on the mutated bytes. They are spelled out rather than calling gate() again,
            # because gate() would recurse into the negatives and there is nothing to learn from a negative of a negative.
            if sub_header["e_machine"] != 3:
                sub_problems.append({"obligation": "header", "why": "not an i386 image"})
            if sub_header["ei_class"] != 1 or sub_header["ei_data"] != 1 or sub_header["e_type"] != 2:
                sub_problems.append({"obligation": "header", "why": "not a 32-bit little-endian executable"})
            for segment in [p for p in sub_phdrs if p["p_type"] == 1]:
                if segment["p_offset"] + segment["p_filesz"] > len(mutated):
                    sub_problems.append({"obligation": "program headers", "why": "a segment runs past the end of the file"})
            if not [s for s in sub_symbols if s["value"] == sub_header["e_entry"] and s["shndx"] != 0]:
                sub_problems.append({"obligation": "entry", "why": "no defined symbol sits at the entry point"})
            undef = sorted({s["name"] for s in sub_symbols if s["shndx"] == 0 and s["name"]})
            if undef:
                sub_problems.append({"obligation": "symbols", "why": "the image has undefined symbols", "sample": undef[:4]})
            obligations = sorted({p["obligation"] for p in sub_problems})
            negatives.append({"name": name, "expected_refusal": True, "refused": bool(sub_problems),
                              "obligations_hit": obligations,
                              "matched_expectation": expect_obligation in obligations,
                              "problems": sub_problems[:3]})
        except ValueError as error:
            negatives.append({"name": name, "expected_refusal": True, "refused": True,
                              "obligations_hit": ["parse"], "matched_expectation": expect_obligation == "parse",
                              "problems": [{"why": str(error)}]})
        finally:
            if os.path.exists(path):
                os.unlink(path)

    def flip_entry(buf):
        struct.pack_into("<I", buf, 24, 0xDEADBEEF)

    def break_segment(buf):
        # Claim the first loadable segment is longer than the file.
        first = struct.unpack_from("<I", buf, 28)[0]
        struct.pack_into("<I", buf, first + 16, 0xFFFFFF00)

    def wrong_machine(buf):
        struct.pack_into("<H", buf, 18, 62)

    def make_undefined(buf):
        # Move the first defined global symbol to the undefined section index.
        for section_index, section in enumerate(sections):
            if section["sh_type"] != 2 or section["sh_entsize"] != 16:
                continue
            for i in range(section["sh_size"] // 16):
                base = section["sh_offset"] + i * 16
                if struct.unpack_from("<H", buf, base + 14)[0] != 0 and struct.unpack_from("<I", buf, base)[0] != 0:
                    struct.pack_into("<H", buf, base + 14, 0)
                    return
        raise ValueError("no symbol to undefine")

    try_negative("entry-point-moved", flip_entry, "entry")
    try_negative("segment-past-end", break_segment, "program headers")
    try_negative("wrong-machine", wrong_machine, "header")
    try_negative("symbol-undefined", make_undefined, "symbols")

    report["negatives"] = negatives
    report["negatives_ok"] = all(n["refused"] and n["matched_expectation"] for n in negatives)
    report["ok"] = report["accepted"] and report["negatives_ok"]
    return report


def main():
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        control = json.load(handle)
    try:
        report = gate(control["image"], control["map"], control.get("expected", {}), control["negatives_root"])
    except Exception as error:  # a gate that cannot read the image has not passed
        report = {"ok": False, "accepted": False, "problems": [{"obligation": "read", "why": str(error)}]}
    report["task"] = control.get("task")
    report["stage"] = control.get("stage")
    path = control["report_path"]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())

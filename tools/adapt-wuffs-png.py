#!/usr/bin/env python3
"""Derive the pinned Wuffs 0.3.5 implementation for GTOS's concrete PNG API.

This is a deliberately modified upstream source, not a general Wuffs drop-in.
The source cache is read-only input. Generated source, a complete unified diff,
and a deterministic hash manifest are written to the requested output directory.
"""

import argparse
import difflib
import hashlib
import json
import pathlib
import re
import sys


UPSTREAM_SHA256 = "82c6741dd751eb962a287882991836498526eb02dbe0c7f19adc01810b8bac96"
UPSTREAM_BYTES = 1825612
ADAPTER_REVISION = 1


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def brace_end(source, start):
    """Find a matching C brace, excluding braces in comments and literals."""
    require(source[start] == "{", "brace parser must start at an opening brace")
    depth = 0
    cursor = start
    while cursor < len(source):
        if source.startswith("//", cursor):
            stop = source.find("\n", cursor + 2)
            cursor = len(source) if stop < 0 else stop + 1
            continue
        if source.startswith("/*", cursor):
            stop = source.find("*/", cursor + 2)
            require(stop >= 0, "unterminated C comment")
            cursor = stop + 2
            continue
        character = source[cursor]
        if character in "\"'":
            quote = character
            cursor += 1
            while cursor < len(source):
                character = source[cursor]
                cursor += 1
                if character == "\\":
                    cursor += 1
                elif character == quote:
                    break
            else:
                raise ValueError("unterminated C literal")
            continue
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return cursor + 1
        cursor += 1
    raise ValueError("unmatched C brace")


def function_body(source, name):
    # Excluding semicolons prevents a prototype from consuming the later body.
    matches = list(re.finditer(r"(?m)^" + re.escape(name) + r"\([^;{}]*\)\s*\{", source))
    require(len(matches) == 1, name + ": expected exactly one actual function definition")
    start = matches[0].end() - 1
    return start, brace_end(source, start)


def adapt(source, pixel_specialization):
    patches = []

    def patch(start, end, replacement, name, purpose):
        original = source[start:end]
        require(original != replacement, name + ": replacement must change source")
        patches.append((start, end, replacement, {
            "name": name,
            "purpose": purpose,
            "replacement_count": 1,
            "upstream_start_byte": len(source[:start].encode("utf-8")),
            "upstream_end_byte": len(source[:end].encode("utf-8")),
            "upstream_sha256": sha(original.encode("utf-8")),
            "derived_sha256": sha(replacement.encode("utf-8")),
        }))

    table_header = "WUFFS_CRC32__IEEE_TABLE[16][256] WUFFS_BASE__POTENTIALLY_UNUSED = "
    require(source.count(table_header) == 1, "expected one sixteen-row CRC32 table")
    table_start = source.index(table_header)
    table_brace = table_start + len(table_header)
    table_end = brace_end(source, table_brace)
    row_start = source.index("{", table_brace + 1)
    row_end = brace_end(source, row_start)
    row = source[row_start:row_end]
    values = [int(value) for value in re.findall(r"\b\d+\b", row)]
    require(len(values) == 256, "CRC32 first row must contain 256 constants")
    for index, actual in enumerate(values):
        expected = index
        for _ in range(8):
            expected = (expected >> 1) ^ (0xEDB88320 if expected & 1 else 0)
        require(actual == expected, "CRC32 table polynomial mismatch")
    table = ("WUFFS_CRC32__IEEE_TABLE[1][256] WUFFS_BASE__POTENTIALLY_UNUSED = {\n"
             "  " + row + ",\n}")
    patch(table_start, table_end, table, "crc32-one-row-table",
          "Keep the exact upstream IEEE polynomial row; discard unused slicing rows.")

    crc_name = "wuffs_crc32__ieee_hasher__up__choosy_default"
    start, end = function_body(source, crc_name)
    crc_body = """{
  // GTOS adaptation: bytewise IEEE CRC32, preserving rolling complemented state.
  uint32_t v_s = UINT32_C(0xFFFFFFFF) ^ self->private_impl.f_state;
  for (size_t i = 0; i < a_x.len; ++i) {
    v_s = WUFFS_CRC32__IEEE_TABLE[0][(uint8_t)v_s ^ a_x.ptr[i]] ^ (v_s >> 8);
  }
  self->private_impl.f_state = UINT32_C(0xFFFFFFFF) ^ v_s;
  return wuffs_base__make_empty_struct();
}"""
    patch(start, end, crc_body, "crc32-bytewise-default-update",
          "Compute the same CRC for arbitrary chunk boundaries and rolling updates.")

    initializers = (
        ("wuffs_adler32__hasher", "hasher_u32"),
        ("wuffs_crc32__ieee_hasher", "hasher_u32"),
        ("wuffs_deflate__decoder", "io_transformer"),
        ("wuffs_zlib__decoder", "io_transformer"),
        ("wuffs_png__decoder", "image_decoder"),
    )
    for concrete, interface in initializers:
        name = concrete + "__initialize"
        body_start, body_end = function_body(source, name)
        registration = (
            "  self->private_impl.vtable_for__wuffs_base__" + interface + ".vtable_name =\n"
            "      wuffs_base__" + interface + "__vtable_name;\n"
            "  self->private_impl.vtable_for__wuffs_base__" + interface + ".function_pointers =\n"
            "      (const void*)(&" + concrete + "__func_ptrs_for__wuffs_base__" + interface + ");"
        )
        body = source[body_start:body_end]
        require(body.count(registration) == 1, name + ": expected one exact vtable registration")
        start = body_start + body.index(registration)
        patch(start, start + len(registration),
              "  // GTOS adaptation: concrete API only; generic vtable stays zero-initialized.",
              concrete + "-concrete-initialization",
              "Remove only unused generic registration; preserve layout, zeroing, magic and real initialization.")

    if pixel_specialization:
        name = "wuffs_base__pixel_swizzler__prepare"
        start, end = function_body(source, name)
        body = source[start:end]
        # Keep both argument checks, all source dispatch cases, and all upstream
        # helper implementations. Expose the actual destination/blend constants.
        blend_start = body.index("  switch (blend) {")
        src_start = body.index("  switch (src_pixfmt.repr) {")
        src_end = brace_end(body, body.index("{", src_start))
        dispatch = body[src_start:src_end]
        require(len(re.findall(r"case WUFFS_BASE__PIXEL_FORMAT__", dispatch)) == 13,
                "pixel preparation must preserve all thirteen upstream source cases")
        helpers = re.findall(r"func = (wuffs_base__pixel_swizzler__prepare__[a-z0-9_]+)\(", dispatch)
        require(len(helpers) == 13 and len(set(helpers)) == 13,
                "pixel preparation must preserve all thirteen upstream helper calls")
        require(dispatch.count("dst_pixfmt") == 13 and dispatch.count("blend") == 13,
                "pixel helper argument replacement count mismatch")

        branches = []
        for destination in ("BGRA_NONPREMUL", "INDEXED__BGRA_NONPREMUL"):
            fixed = "wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__" + destination + ")"
            specialized = dispatch.replace("dst_pixfmt", fixed).replace("blend", "WUFFS_BASE__PIXEL_BLEND__SRC")
            branches.append("    case WUFFS_BASE__PIXEL_FORMAT__" + destination + ":\n" +
                            "\n".join("    " + line for line in specialized.splitlines()) + "\n      break;")
        restricted = """  // GTOS adaptation: the concrete PNG raster API always uses SRC and one
  // of these two destinations. All thirteen upstream source cases remain.
  if ((blend != WUFFS_BASE__PIXEL_BLEND__SRC) ||
      ((dst_pixfmt.repr != WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL) &&
       (dst_pixfmt.repr != WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_NONPREMUL))) {
    return wuffs_base__make_status(
        wuffs_base__error__unsupported_pixel_swizzler_option);
  }
  transparent_black_func = wuffs_base__pixel_swizzler__transparent_black_src;
  switch (dst_pixfmt.repr) {
""" + "\n".join(branches) + "\n  }"
        modified = body[:blend_start] + restricted + body[src_end:]
        patch(start, end, modified, "pixel-prepare-concrete-destinations",
              "Retain all source formats and upstream conversions for SRC into BGRA or indexed BGRA.")

    patches.sort(key=lambda item: item[0])
    for previous, current in zip(patches, patches[1:]):
        require(previous[1] <= current[0], "source adaptations must not overlap")
    require(len(patches) == (8 if pixel_specialization else 7), "total adaptation count mismatch")
    pieces = []
    cursor = 0
    for start, end, replacement, _ in patches:
        pieces.extend((source[cursor:start], replacement))
        cursor = end
    pieces.append(source[cursor:])
    derived = "".join(pieces)
    # Every high-row reference was inside the replaced scalar CRC body.
    references = re.findall(r"WUFFS_CRC32__IEEE_TABLE\[(\d+)\]\[", derived)
    require(references.count("1") == 1 and all(value in ("0", "1") for value in references),
            "derived CRC table still has a high-row reference")
    require(source[:source.index("#ifndef WUFFS_INCLUDE_GUARD")] ==
            derived[:derived.index("#ifndef WUFFS_INCLUDE_GUARD")],
            "upstream license/header prefix must remain unchanged")
    return derived, [item[3] for item in patches]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path, help="read-only pinned wuffs-v0.3.c")
    parser.add_argument("output", type=pathlib.Path, help="generated-source directory")
    parser.add_argument("--no-pixel-specialization", action="store_true",
                        help="produce the CRC/vtable candidate for independent size comparison")
    args = parser.parse_args()
    raw = args.source.read_bytes()
    require(len(raw) == UPSTREAM_BYTES and sha(raw) == UPSTREAM_SHA256,
            "input must be the exact pinned upstream Wuffs 0.3.5 source")
    source = raw.decode("utf-8")
    require("\r" not in source and source.encode("utf-8") == raw,
            "pinned source must retain exact UTF-8/LF encoding")
    pixel_specialization = not args.no_pixel_specialization
    derived, changes = adapt(source, pixel_specialization)
    generated = derived.encode("utf-8")
    diff = "".join(difflib.unified_diff(source.splitlines(keepends=True),
                                      derived.splitlines(keepends=True),
                                      fromfile="upstream/wuffs-v0.3.c",
                                      tofile="derived/wuffs-png-gtos.c"))
    manifest = {
        "adapter_revision": ADAPTER_REVISION,
        "upstream": {"version": "0.3.5", "bytes": len(raw), "sha256": sha(raw)},
        "derived": {"file": "wuffs-png-gtos.c", "bytes": len(generated), "sha256": sha(generated)},
        "diff": {"file": "wuffs-png-gtos.diff", "sha256": sha(diff.encode("utf-8"))},
        "adapter_sha256": sha(pathlib.Path(__file__).read_bytes()),
        "function_bodies_modified": True,
        "generic_upcasting_supported": False,
        "upstream_struct_layouts_modified": False,
        "upstream_vtable_definitions_modified": False,
        "pixel_specialization": pixel_specialization,
        "pixel_blend": "SRC" if pixel_specialization else "upstream",
        "pixel_destinations": ["BGRA_NONPREMUL", "INDEXED__BGRA_NONPREMUL"] if pixel_specialization else "upstream",
        "source_dispatch_cases_preserved": 13,
        "adaptation_count": len(changes),
        "adaptations": changes,
    }
    for filename in ("wuffs-png-gtos.c", "wuffs-png-gtos.diff", "wuffs-png-gtos.manifest.json"):
        require(args.source.resolve() != (args.output / filename).resolve(),
                "the upstream input must never be overwritten")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "wuffs-png-gtos.c").write_bytes(generated)
    (args.output / "wuffs-png-gtos.diff").write_bytes(diff.encode("utf-8"))
    (args.output / "wuffs-png-gtos.manifest.json").write_bytes(
        (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    print(json.dumps({"derived_sha256": sha(generated), "adaptation_count": len(changes),
                      "pixel_specialization": pixel_specialization}, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, UnicodeError, ValueError) as error:
        print("Wuffs adaptation failed: " + str(error), file=sys.stderr)
        sys.exit(1)

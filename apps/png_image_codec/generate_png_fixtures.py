#!/usr/bin/env python3
"""Deterministic, dependency-free PNG encoder and source-pixel oracle.

No component decoder is imported or called. Python's zlib is used only to
cross-check our independently emitted RFC 1950/1951 streams, never to encode.
All expected pixels come from the source sample tuples below.
"""

import argparse
import binascii
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib


SIGNATURE = b"\x89PNG\r\n\x1a\n"
ADAM7 = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8),
         (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))
SAMPLES = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
LENGTH_BASE = (3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23,
               27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163,
               195, 227, 258)
LENGTH_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0)
DIST_BASE = (1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97,
             129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
             4097, 6145, 8193, 12289, 16385, 24577)
DIST_EXTRA = (0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13)
CL_ORDER = (16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12,
            3, 13, 2, 14, 1, 15)
SPEC_URL = "https://www.w3.org/TR/2025/REC-png-3-20250624/"


def chunk(kind, data=b""):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff))


def canonical_codes(lengths):
    """Return conventional MSB-first canonical Huffman codes by symbol."""
    counts = [0] * (max(lengths) + 1)
    for length in lengths:
        if length:
            counts[length] += 1
    next_code = [0] * len(counts)
    code = 0
    for bits in range(1, len(counts)):
        code = (code + counts[bits - 1]) << 1
        next_code[bits] = code
    assert code + counts[-1] == 1 << (len(counts) - 1), "incomplete tree"
    result = []
    for length in lengths:
        result.append((next_code[length], length) if length else (0, 0))
        if length:
            next_code[length] += 1
    return result


class Bits:
    def __init__(self):
        self.data = bytearray()
        self.pending = 0
        self.count = 0

    def put(self, value, count):
        """RFC 1951 non-code fields are least-significant-bit first."""
        assert 0 <= value < 1 << count
        self.pending |= value << self.count
        self.count += count
        while self.count >= 8:
            self.data.append(self.pending & 255)
            self.pending >>= 8
            self.count -= 8

    def symbol(self, table, symbol):
        """Huffman codes are transmitted most-significant-bit first."""
        value, count = table[symbol]
        assert count
        for bit in range(count - 1, -1, -1):
            self.put((value >> bit) & 1, 1)

    def finish(self):
        if self.count:
            self.data.append(self.pending)
            self.pending = self.count = 0
        return bytes(self.data)


def lz77(raw):
    """Small deterministic greedy encoder with real overlapping matches."""
    tokens = []
    position = 0
    while position < len(raw):
        best_length = best_distance = 0
        for distance in range(1, min(position, 512) + 1):
            length = 0
            maximum = min(258, len(raw) - position)
            while (length < maximum and
                   raw[position + length] == raw[position + length - distance]):
                length += 1
            if length > best_length and length >= 3:
                best_length, best_distance = length, distance
                if length == maximum:
                    break
        if best_length:
            tokens.append((best_length, best_distance))
            position += best_length
        else:
            tokens.append(raw[position])
            position += 1
    return tokens


def choose_code(value, bases, extras):
    for index, (base, extra) in enumerate(zip(bases, extras)):
        maximum = base + (1 << extra) - 1
        if index + 1 < len(bases):
            maximum = min(maximum, bases[index + 1] - 1)
        if base <= value <= maximum:
            return index, value - base, extra
    raise ValueError(value)


def adler32(raw):
    a, b = 1, 0
    for value in raw:
        a = (a + value) % 65521
        b = (b + a) % 65521
    return (b << 16) | a


def zlib_stream(raw, mode):
    """One stable zlib block, independent of host compressor heuristics."""
    assert len(raw) <= 65535
    matches = 0
    if mode == "stored":
        deflate = b"\x01" + struct.pack("<HH", len(raw), len(raw) ^ 65535) + raw
        block_type = 0
    else:
        bits = Bits()
        bits.put(1, 1)  # BFINAL
        block_type = {"fixed": 1, "dynamic": 2}[mode]
        bits.put(block_type, 2)
        if mode == "fixed":
            literals = canonical_codes([8] * 144 + [9] * 112 + [7] * 24 + [8] * 8)
            distances = canonical_codes([5] * 32)
        else:
            # These are complete dynamic trees with no reserved literal codes.
            # Stable handcrafted trees avoid a host zlib-version dependency.
            literal_lengths = [8] * 144 + [9] * 108 + [7] * 24 + [8] * 10
            distance_lengths = [4] * 2 + [5] * 28
            code_lengths = [4] * 13 + [5] * 6
            lengths_table = canonical_codes(code_lengths)
            bits.put(len(literal_lengths) - 257, 5)
            bits.put(len(distance_lengths) - 1, 5)
            bits.put(19 - 4, 4)
            for symbol in CL_ORDER:
                bits.put(code_lengths[symbol], 3)
            for length in literal_lengths + distance_lengths:
                bits.symbol(lengths_table, length)
            literals = canonical_codes(literal_lengths)
            distances = canonical_codes(distance_lengths)
        for token in lz77(raw):
            if isinstance(token, int):
                bits.symbol(literals, token)
                continue
            length, distance = token
            matches += 1
            index, extra, count = choose_code(length, LENGTH_BASE, LENGTH_EXTRA)
            bits.symbol(literals, 257 + index)
            bits.put(extra, count)
            index, extra, count = choose_code(distance, DIST_BASE, DIST_EXTRA)
            bits.symbol(distances, index)
            bits.put(extra, count)
        bits.symbol(literals, 256)
        deflate = bits.finish()
    stream = b"\x78\x01" + deflate + struct.pack(">I", adler32(raw))
    assert ((stream[2] >> 1) & 3) == block_type
    assert zlib.decompress(stream) == raw, "independent host inflate failed"
    return stream, block_type, matches


@dataclass(frozen=True)
class Source:
    name: str
    rows: tuple
    color_type: int = 6
    depth: int = 8
    filters: tuple = (0,)
    mode: str = "fixed"
    interlace: int = 0
    palette: tuple = ()
    transparency: tuple = ()
    split_idat: bool = False
    ancillary: bool = False

    @property
    def width(self):
        return len(self.rows[0])

    @property
    def height(self):
        return len(self.rows)


def sample_byte(value, depth):
    """Exact nearest 8-bit rescaling; 16-bit fixtures use multiples of 257."""
    maximum = (1 << depth) - 1
    assert 0 <= value <= maximum
    return (value * 255 + maximum // 2) // maximum


def source_rgba(source):
    """Pixel oracle reads source tuples, never encoded or decoded bytes."""
    output = []
    for row in source.rows:
        for pixel in row:
            if source.color_type == 3:
                index = pixel[0]
                rgb = source.palette[index]
                alpha = (source.transparency[index]
                         if index < len(source.transparency) else 255)
                rgba = (*rgb, alpha)
            elif source.color_type == 6:
                rgba = tuple(sample_byte(v, source.depth) for v in pixel)
            elif source.color_type == 2:
                rgba = (*(sample_byte(v, source.depth) for v in pixel),
                        0 if pixel == source.transparency else 255)
            elif source.color_type == 4:
                gray, alpha = (sample_byte(v, source.depth) for v in pixel)
                rgba = (gray, gray, gray, alpha)
            elif source.color_type == 0:
                gray = sample_byte(pixel[0], source.depth)
                alpha = 0 if pixel == source.transparency else 255
                rgba = (gray, gray, gray, alpha)
            else:
                raise ValueError(source.color_type)
            output.append(rgba)
    return output


def expected_pixels(source):
    bgra, premul = bytearray(), bytearray()
    for red, green, blue, alpha in source_rgba(source):
        bgra.extend((blue, green, red, alpha))
        # A mathematical nearest-integer oracle, independent of Skia's shifts.
        premul.extend(((red * alpha + 127) // 255,
                       (green * alpha + 127) // 255,
                       (blue * alpha + 127) // 255, alpha))
    return bytes(bgra), bytes(premul)


def pack_row(row, depth):
    samples = [sample for pixel in row for sample in pixel]
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", sample) for sample in samples)
    result = bytearray()
    pending, count = 0, 0
    for sample in samples:
        assert 0 <= sample < 1 << depth
        pending = (pending << depth) | sample
        count += depth
        if count == 8:
            result.append(pending)
            pending = count = 0
    if count:
        result.append(pending << (8 - count))
    return bytes(result)


def paeth(left, above, corner):
    prediction = left + above - corner
    distances = (abs(prediction - left), abs(prediction - above),
                 abs(prediction - corner))
    # Tuple lookup implements specified a, then b, then c tie order.
    return (left, above, corner)[distances.index(min(distances))]


def filter_row(row, previous, bpp, kind):
    result = bytearray((kind,))
    for index, value in enumerate(row):
        left = row[index - bpp] if index >= bpp else 0
        above = previous[index] if previous else 0
        corner = previous[index - bpp] if previous and index >= bpp else 0
        predictor = (0, left, above, (left + above) // 2,
                     paeth(left, above, corner))[kind]
        result.append((value - predictor) & 255)
    return bytes(result)


def filtered_scanlines(source):
    passes = ADAM7 if source.interlace else ((0, 0, 1, 1),)
    result = bytearray()
    used = 0
    line_number = 0
    bpp = max(1, (SAMPLES[source.color_type] * source.depth + 7) // 8)
    for start_x, start_y, step_x, step_y in passes:
        if start_x >= source.width or start_y >= source.height:
            continue
        previous = b""
        for y in range(start_y, source.height, step_y):
            row = pack_row(source.rows[y][start_x::step_x], source.depth)
            kind = source.filters[line_number % len(source.filters)]
            used |= 1 << kind
            result.extend(filter_row(row, previous, bpp, kind))
            previous = row
            line_number += 1
    return bytes(result), used


def ihdr(source):
    return struct.pack(">IIBBBBB", source.width, source.height, source.depth,
                       source.color_type, 0, 0, source.interlace)


def source_chunks(source, stream):
    result = [(b"IHDR", ihdr(source))]
    if source.palette:
        result.append((b"PLTE", bytes(v for rgb in source.palette for v in rgb)))
    if source.transparency:
        payload = (bytes(source.transparency) if source.color_type == 3 else
                   b"".join(struct.pack(">H", v) for v in source.transparency))
        result.append((b"tRNS", payload))
    if source.ancillary:
        result.append((b"raNd", b"GTOS fixture"))
    if source.split_idat:
        # Split zlib header, DEFLATE, and Adler checksum; include an empty IDAT.
        cuts = (0, 1, 1, 3, len(stream) - 2, len(stream))
        for start, end in zip(cuts, cuts[1:]):
            result.append((b"IDAT", stream[start:end]))
    else:
        result.append((b"IDAT", stream))
    result.append((b"IEND", b""))
    return result


def encode_chunks(chunks):
    return SIGNATURE + b"".join(chunk(kind, data) for kind, data in chunks)


@dataclass(frozen=True)
class Fixture:
    name: str
    data: bytes
    classification: str
    reason: str
    width: int = 0
    height: int = 0
    depth: int = 0
    color_type: int = 0
    interlace: int = 0
    filter_mask: int = 0
    block_type: int = 0xffffffff
    matches: int = 0
    raw_size: int = 0
    bgra: bytes = b""
    premul: bytes = b""


def positive(source):
    raw, mask = filtered_scanlines(source)
    stream, block_type, matches = zlib_stream(raw, source.mode)
    bgra, premul = expected_pixels(source)
    return Fixture(source.name, encode_chunks(source_chunks(source, stream)),
                   "conforming", "source-pixel oracle", source.width,
                   source.height, source.depth, source.color_type,
                   source.interlace, mask, block_type, matches, len(raw),
                   bgra, premul)


def sources():
    alpha = (0, 1, 127, 128, 254, 255)
    filters = tuple(tuple(((37*x + 91*y + 17) & 255,
                           (113*x + 43*y + 241) & 255,
                           (61*x + 151*y + 5) & 255,
                           alpha[(x + y) % len(alpha)])
                          for x in range(4)) for y in range(5))
    endpoints = (((255, 91, 17, 0), (255, 128, 1, 1),
                  (128, 255, 254, 127), (255, 1, 128, 128)),
                 ((1, 255, 128, 254), (19, 71, 233, 255),
                  (255, 255, 255, 255), (0, 0, 0, 0)))
    repeated = tuple(tuple((x * 17, x * 9, 255 - x * 23,
                            alpha[x % len(alpha)]) for x in range(8))
                     for _ in range(8))
    palette = ((239, 31, 7), (3, 211, 53), (11, 61, 251),
               (167, 113, 71), (255, 255, 255))
    yield Source("rgba8_filters_fixed", filters, filters=(0, 1, 2, 3, 4))
    yield Source("rgba8_stored_alpha", endpoints, mode="stored")
    yield Source("rgba8_dynamic_matches", repeated, mode="dynamic")
    yield Source("rgb8_filters_dynamic",
                 tuple(tuple(((19*x + 23*y) & 255, (53*x + 71*y) & 255,
                              (241 - 11*x - 29*y) & 255) for x in range(3))
                       for y in range(5)), color_type=2,
                 filters=(0, 1, 2, 3, 4), mode="dynamic")
    yield Source("gray8_fixed", (((0,), (1,), (127,), (128,), (255,)),
                                 ((255,), (254,), (129,), (2,), (17,))),
                 color_type=0, filters=(1, 4))
    yield Source("palette8_trns_stored",
                 tuple(tuple(((x + y) % 5,) for x in range(4)) for y in range(3)),
                 color_type=3, palette=palette, transparency=(0, 1, 128, 254),
                 filters=(0, 1, 2), mode="stored")
    yield Source("rgba8_adam7_fixed",
                 tuple(tuple(((x*53 + y*7) & 255, (x*11 + y*71) & 255,
                              (251 - x*19 - y*31) & 255, alpha[(x+y) % 6])
                             for x in range(5)) for y in range(5)),
                 interlace=1, filters=(4, 3, 2, 1, 0))
    yield Source("rgba8_split_idat_ancillary", tuple(row[:3] for row in endpoints),
                 split_idat=True, ancillary=True, filters=(4, 2))
    yield Source("rgb8_trns_fixed",
                 (((17, 33, 65), (17, 33, 66), (255, 1, 7)),
                  ((0, 0, 0), (17, 33, 65), (1, 2, 3))), color_type=2,
                 transparency=(17, 33, 65), filters=(3, 4))
    yield Source("gray8_trns_stored",
                 (((0,), (127,), (128,), (255,)),
                  ((128,), (129,), (1,), (128,))), color_type=0,
                 transparency=(128,), filters=(0, 2), mode="stored")
    yield Source("grayalpha8_fixed",
                 (((255, 0), (128, 1), (17, 127), (255, 128)),
                  ((255, 254), (9, 255), (0, 255), (71, 0))),
                 color_type=4, filters=(1, 3))
    yield Source("gray1_packed_stored",
                 (tuple((x % 2,) for x in range(9)),
                  tuple(((x + 1) % 2,) for x in range(9))),
                 color_type=0, depth=1, filters=(0, 1), mode="stored")
    yield Source("palette2_packed_trns_fixed",
                 (((0,), (1,), (2,), (3,), (0,)),
                  ((3,), (2,), (1,), (0,), (3,))), color_type=3, depth=2,
                 palette=palette[:4], transparency=(0, 128), filters=(0, 4))
    yield Source("rgba16_exact_scale_fixed",
                 tuple(tuple(tuple(value * 257 for value in pixel)
                             for pixel in row[:2]) for row in endpoints),
                 depth=16, filters=(1, 4))
    yield Source("rgba8_adam7_empty_passes", (((241, 13, 7, 128),),),
                 interlace=1, mode="stored", filters=(4,))


def make_fixtures():
    inputs = list(sources())
    result = [positive(source) for source in inputs]
    tiny = Source("tiny", (((17, 33, 65, 128),),), mode="stored")
    raw, _ = filtered_scanlines(tiny)
    stream, _, _ = zlib_stream(raw, tiny.mode)
    basic = source_chunks(tiny, stream)
    good = encode_chunks(basic)

    def bad(name, data, reason):
        metadata = {}
        if len(data) >= 29 and data[12:16] == b"IHDR" and data[8:12] == b"\0\0\0\r":
            width, height, depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", data[16:29])
            metadata = dict(width=width, height=height, depth=depth,
                            color_type=color_type, interlace=interlace)
        result.append(Fixture(name, data, "malformed", reason, **metadata))

    def chunks_bad(name, chunks, reason):
        bad(name, encode_chunks(chunks), reason)

    def header_bad(name, offset, payload, reason):
        header = bytearray(ihdr(tiny))
        header[offset:offset+len(payload)] = payload
        chunks_bad(name, [(b"IHDR", bytes(header))] + basic[1:], reason)

    bad("bad_signature", b"X" + good[1:], "incorrect PNG signature")
    corrupted = bytearray(good)
    corrupted[29] ^= 1
    bad("bad_ihdr_crc", bytes(corrupted), "IHDR CRC mismatch")
    corrupted = bytearray(good)
    corrupted[41 + len(stream)] ^= 1
    bad("bad_idat_crc", bytes(corrupted), "IDAT CRC mismatch")
    corrupted = bytearray(good)
    corrupted[33:37] = struct.pack(">I", len(stream) + 1000)
    bad("chunk_length_past_input", bytes(corrupted), "IDAT length exceeds input")
    corrupted = bytearray(good)
    corrupted[33:37] = b"\x80\x00\x00\x00"
    bad("chunk_length_high_bit", bytes(corrupted), "chunk length exceeds 2^31-1")
    bad("truncated_iend", good[:-3], "partial IEND CRC")
    bad("missing_iend", good[:-12], "required IEND absent")
    chunks_bad("missing_idat", [basic[0], basic[-1]], "required IDAT absent")
    chunks_bad("truncated_zlib", [basic[0], (b"IDAT", stream[:-2]), basic[-1]],
               "truncated zlib Adler checksum, valid chunk CRC")
    short_deflate = stream[:8]
    chunks_bad("truncated_deflate", [basic[0], (b"IDAT", short_deflate), basic[-1]],
               "stored DEFLATE payload truncated, valid chunk CRC")
    corrupted = stream[:-1] + bytes((stream[-1] ^ 1,))
    chunks_bad("bad_adler", [basic[0], (b"IDAT", corrupted), basic[-1]],
               "zlib Adler checksum mismatch, valid chunk CRC")
    corrupted = stream[:2] + b"\x07" + stream[3:]
    chunks_bad("deflate_reserved_btype", [basic[0], (b"IDAT", corrupted), basic[-1]],
               "reserved DEFLATE block type 3")
    for name, offset, payload, reason in (
            ("width_zero", 0, b"\0\0\0\0", "IHDR width is zero"),
            ("height_zero", 4, b"\0\0\0\0", "IHDR height is zero"),
            ("width_high_bit", 0, b"\x80\0\0\0", "IHDR width exceeds 2^31-1"),
            ("height_high_bit", 4, b"\x80\0\0\0", "IHDR height exceeds 2^31-1"),
            ("rgba_depth_four", 8, b"\x04", "bit depth 4 invalid for RGBA"),
            ("reserved_color_type_one", 9, b"\x01", "reserved color type 1"),
            ("reserved_compression_one", 10, b"\x01", "reserved compression method 1"),
            ("reserved_filter_method_one", 11, b"\x01", "reserved filter method 1"),
            ("reserved_interlace_two", 12, b"\x02", "reserved interlace method 2")):
        header_bad(name, offset, payload, reason)
    for name, raw_bad, reason in (
            ("invalid_scanline_filter", b"\x05" + raw[1:], "filter type 5 is forbidden"),
            ("too_few_scanline_bytes", raw[:-1], "inflated scanline is too short"),
            ("extra_scanline_bytes", raw + b"\0", "inflated data exceeds image scanlines")):
        encoded, _, _ = zlib_stream(raw_bad, "stored")
        chunks_bad(name, [basic[0], (b"IDAT", encoded), basic[-1]], reason)
    chunks_bad("duplicate_ihdr", [basic[0], basic[0]] + basic[1:],
               "duplicate critical IHDR")
    chunks_bad("rgba_forbidden_trns", [basic[0], (b"tRNS", b"\0\0")] + basic[1:],
               "tRNS prohibited for RGBA color type 6")
    indexed = Source("indexed", (((0,),),), color_type=3,
                     palette=((17, 33, 65),), mode="stored")
    indexed_raw, _ = filtered_scanlines(indexed)
    indexed_stream, _, _ = zlib_stream(indexed_raw, "stored")
    indexed_chunks = source_chunks(indexed, indexed_stream)
    chunks_bad("indexed_missing_plte", [c for c in indexed_chunks if c[0] != b"PLTE"],
               "indexed-color requires PLTE")
    chunks_bad("plte_bad_length", [(k, d[:-1] if k == b"PLTE" else d)
                                   for k, d in indexed_chunks],
               "PLTE length not divisible by three")
    encoded, _, _ = zlib_stream(b"\0\x01", "stored")
    chunks_bad("palette_index_out_of_range", [(k, encoded if k == b"IDAT" else d)
                                              for k, d in indexed_chunks],
               "pixel index exceeds one-entry palette")
    chunks_bad("palette_trns_too_long", indexed_chunks[:2] +
               [(b"tRNS", b"\0\xff")] + indexed_chunks[2:],
               "tRNS has more entries than PLTE")
    # Extension semantics are unknown, so this is unsupported, never corrupt.
    result.append(Fixture("unknown_private_critical_extension",
                          encode_chunks([basic[0], (b"VpAg", b"GTOS")] + basic[1:]),
                          "requires_extension", "unknown private critical chunk VpAg",
                          width=1, height=1, depth=8, color_type=6))
    assert len({f.name for f in result}) == len(result)
    assert sum(len(f.data) + len(f.bgra) + len(f.premul) for f in result) < 12 * 1024
    assert any(f.block_type == 2 and f.matches for f in result)
    return result


def c_array(name, data):
    lines = ["static const uint8_t %s[] = {" % name]
    for offset in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % value
                                       for value in data[offset:offset+16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def render_header(fixtures):
    output = ["/* Generated by generate_png_fixtures.py; do not edit. */",
              "/* Expected pixels derive from source tuples, independently of the decoder. */",
              "#ifndef GTOS_PNG_FIXTURES_H", "#define GTOS_PNG_FIXTURES_H",
              "#include <stdint.h>", "",
              "enum gtos_png_fixture_class {",
              "    GTOS_PNG_CONFORMING = 0,",
              "    GTOS_PNG_MALFORMED = 1,",
              "    GTOS_PNG_REQUIRES_EXTENSION = 2", "};", "",
              "typedef struct gtos_png_fixture {",
              "    const char* name;", "    const uint8_t* png;",
              "    const uint8_t* expected_bgra;",
              "    const uint8_t* expected_premul_rgba;",
              "    uint32_t png_bytes;", "    uint32_t expected_bytes;",
              "    uint32_t width;", "    uint32_t height;",
              "    uint32_t bit_depth;", "    uint32_t color_type;",
              "    uint32_t interlace;", "    uint32_t filter_mask;",
              "    uint32_t first_deflate_block_type;",
              "    uint32_t deflate_matches;", "    uint32_t inflated_bytes;",
              "    uint32_t classification;", "} gtos_png_fixture;", ""]
    for fixture in fixtures:
        output.append("/* %s: %s. */" % (fixture.name, fixture.reason))
        output.append(c_array("png_" + fixture.name, fixture.data))
        if fixture.bgra:
            output.append(c_array("bgra_" + fixture.name, fixture.bgra))
            output.append(c_array("premul_rgba_" + fixture.name, fixture.premul))
        output.append("")
    output.append("static const gtos_png_fixture gtos_png_fixtures[] = {")
    classifications = {"conforming": "GTOS_PNG_CONFORMING",
                       "malformed": "GTOS_PNG_MALFORMED",
                       "requires_extension": "GTOS_PNG_REQUIRES_EXTENSION"}
    for f in fixtures:
        bgra = "bgra_" + f.name if f.bgra else "0"
        premul = "premul_rgba_" + f.name if f.premul else "0"
        values = (len(f.data), len(f.bgra), f.width, f.height, f.depth,
                  f.color_type, f.interlace, f.filter_mask, f.block_type,
                  f.matches, f.raw_size)
        output.append('    {"%s", png_%s, %s, %s,' % (f.name, f.name, bgra, premul))
        output.append("     " + ", ".join("%du" % v for v in values) +
                      ", " + classifications[f.classification] + "},")
    output.extend(("};", "", "#define GTOS_PNG_FIXTURE_COUNT %du" % len(fixtures),
                   "#define GTOS_PNG_FIXTURE_EMBEDDED_BYTES %du" % sum(
                       len(f.data) + len(f.bgra) + len(f.premul) for f in fixtures),
                   "#define GTOS_PNG_FIXTURE_MAX_PIXELS %du" % max(
                       f.width * f.height for f in fixtures if f.bgra),
                   "#define GTOS_PNG_FIXTURE_MAX_PNG_BYTES %du" % max(
                       len(f.data) for f in fixtures), "", "#endif", ""))
    return "\n".join(output).encode("ascii")


def render_manifest(fixtures):
    manifest = {
        "schema": 1,
        "generator": "generate_png_fixtures.py",
        "sources": [SPEC_URL, "https://www.rfc-editor.org/rfc/rfc1950.html",
                    "https://www.rfc-editor.org/rfc/rfc1951.html"],
        "oracle": "BGRA derived from source samples; premultiplied RGBA channels nearest(c*a/255), alpha unchanged",
        "encoder": "self-contained one-block stored/fixed/dynamic RFC1951; stable greedy LZ77",
        "host_zlib_role": "verification only; no compression or pixel decode",
        "embedded_png_bytes": sum(len(f.data) for f in fixtures),
        "embedded_expected_bytes": sum(len(f.bgra) + len(f.premul) for f in fixtures),
        "embedded_total_bytes": sum(len(f.data) + len(f.bgra) + len(f.premul)
                                    for f in fixtures),
        "fixture_count": len(fixtures),
        "conforming_count": sum(f.classification == "conforming" for f in fixtures),
        "malformed_count": sum(f.classification == "malformed" for f in fixtures),
        "requires_extension_count": sum(f.classification == "requires_extension"
                                         for f in fixtures),
        "fixtures": []}
    for f in fixtures:
        manifest["fixtures"].append({
            "name": f.name, "classification": f.classification, "reason": f.reason,
            "width": f.width, "height": f.height, "bit_depth": f.depth,
            "color_type": f.color_type, "interlace": f.interlace,
            "filter_mask": f.filter_mask, "first_deflate_block_type": f.block_type,
            "deflate_matches": f.matches, "inflated_bytes": f.raw_size,
            "png_bytes": len(f.data), "expected_bytes_per_format": len(f.bgra),
            "png_sha256": hashlib.sha256(f.data).hexdigest(),
            "bgra_sha256": hashlib.sha256(f.bgra).hexdigest() if f.bgra else None,
            "premul_rgba_sha256": hashlib.sha256(f.premul).hexdigest() if f.premul else None})
    return (json.dumps(manifest, indent=2) + "\n").encode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify generated files byte-for-byte; never write")
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    fixtures = make_fixtures()
    outputs = {"png_fixtures.h": render_header(fixtures),
               "png_fixture_manifest.json": render_manifest(fixtures)}
    failed = []
    if not args.check:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, contents in outputs.items():
        target = args.output_dir / name
        if args.check:
            if not target.is_file() or target.read_bytes() != contents:
                failed.append(str(target))
        else:
            target.write_bytes(contents)
    if failed:
        print("PNG fixture check FAILED: " + ", ".join(failed), file=sys.stderr)
        return 1
    print("PNG fixture %s: count=%d conforming=%d malformed=%d extension=%d embedded=%d" % (
        "check PASS" if args.check else "generation PASS", len(fixtures),
        sum(f.classification == "conforming" for f in fixtures),
        sum(f.classification == "malformed" for f in fixtures),
        sum(f.classification == "requires_extension" for f in fixtures),
        sum(len(f.data) + len(f.bgra) + len(f.premul) for f in fixtures)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

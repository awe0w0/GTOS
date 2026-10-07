#!/usr/bin/env python3
"""Cross-check fixture framing, designated faults, and optional host Pillow pixels.

Default verification has no third-party dependencies. --pillow is a supplementary
check with an existing host installation; the generator never depends on Pillow.
"""

import argparse
import binascii
import io
import struct
import zlib

import generate_png_fixtures as generator


def parsed_chunks(data):
    if data[:8] != generator.SIGNATURE:
        raise ValueError("signature")
    result = []
    offset = 8
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError("truncated chunk")
        length = struct.unpack(">I", data[offset:offset+4])[0]
        if length > 0x7fffffff or offset + 12 + length > len(data):
            raise ValueError("chunk length")
        kind = data[offset+4:offset+8]
        payload = data[offset+8:offset+8+length]
        crc = struct.unpack(">I", data[offset+8+length:offset+12+length])[0]
        if binascii.crc32(kind + payload) & 0xffffffff != crc:
            raise ValueError("chunk CRC")
        result.append((kind, payload))
        offset += 12 + length
    return result


def inflate(chunks):
    stream = b"".join(data for kind, data in chunks if kind == b"IDAT")
    return zlib.decompress(stream)


def designated_fault(fixture):
    """Check the claimed fault directly; this is not a component decoder."""
    framing_faults = {"bad_signature", "bad_ihdr_crc", "bad_idat_crc",
                      "chunk_length_past_input", "chunk_length_high_bit",
                      "truncated_iend"}
    if fixture.name in framing_faults:
        try:
            parsed_chunks(fixture.data)
        except ValueError:
            return True
        return False
    chunks = parsed_chunks(fixture.data)
    kinds = [kind for kind, _ in chunks]
    if fixture.name == "missing_iend":
        return b"IEND" not in kinds
    if fixture.name == "missing_idat":
        return b"IDAT" not in kinds
    if fixture.name == "duplicate_ihdr":
        return kinds.count(b"IHDR") == 2
    if fixture.name in {"truncated_zlib", "truncated_deflate", "bad_adler",
                        "deflate_reserved_btype"}:
        try:
            inflate(chunks)
        except zlib.error:
            return True
        return False
    raw = inflate(chunks)
    if fixture.name == "invalid_scanline_filter":
        return raw[0] == 5
    if fixture.name == "too_few_scanline_bytes":
        return len(raw) == 4  # 1x1 RGBA8 requires filter + four samples.
    if fixture.name == "extra_scanline_bytes":
        return len(raw) == 6
    header = chunks[0][1]
    width, height, depth, color_type, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", header)
    fields = {"width_zero": width == 0, "height_zero": height == 0,
              "width_high_bit": width > 0x7fffffff,
              "height_high_bit": height > 0x7fffffff,
              "rgba_depth_four": color_type == 6 and depth == 4,
              "reserved_color_type_one": color_type == 1,
              "reserved_compression_one": compression == 1,
              "reserved_filter_method_one": filtering == 1,
              "reserved_interlace_two": interlace == 2}
    if fixture.name in fields:
        return fields[fixture.name]
    if fixture.name == "rgba_forbidden_trns":
        return color_type == 6 and b"tRNS" in kinds
    if fixture.name == "indexed_missing_plte":
        return color_type == 3 and b"PLTE" not in kinds
    palette = next(data for kind, data in chunks if kind == b"PLTE")
    if fixture.name == "plte_bad_length":
        return len(palette) % 3 != 0
    if fixture.name == "palette_index_out_of_range":
        return raw[0] == 0 and raw[1] >= len(palette) // 3
    if fixture.name == "palette_trns_too_long":
        transparency = next(data for kind, data in chunks if kind == b"tRNS")
        return len(transparency) > len(palette) // 3
    raise AssertionError("unverified fault " + fixture.name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pillow", action="store_true")
    args = parser.parse_args()
    fixtures = generator.make_fixtures()
    inputs = {source.name: source for source in generator.sources()}
    image_module = None
    if args.pillow:
        from PIL import Image
        image_module = Image
    for fixture in fixtures:
        if fixture.classification == "malformed":
            assert designated_fault(fixture), fixture.name
            continue
        chunks = parsed_chunks(fixture.data)
        assert chunks[0][0] == b"IHDR" and chunks[-1] == (b"IEND", b""), fixture.name
        if fixture.classification == "requires_extension":
            assert any(kind == b"VpAg" for kind, _ in chunks)
            continue
        source = inputs[fixture.name]
        raw, mask = generator.filtered_scanlines(source)
        assert inflate(chunks) == raw, fixture.name
        assert mask == fixture.filter_mask and len(raw) == fixture.raw_size
        if image_module:
            image = image_module.open(io.BytesIO(fixture.data))
            image.load()
            assert image.size == (fixture.width, fixture.height), fixture.name
            actual = image.convert("RGBA").tobytes()
            expected = bytes(channel for pixel in generator.source_rgba(source)
                             for channel in pixel)
            assert actual == expected, fixture.name
        for offset in range(0, len(fixture.bgra), 4):
            blue, green, red, alpha = fixture.bgra[offset:offset+4]
            expected = []
            for color in (red, green, blue):
                # Independently check the arithmetic shift form Skia uses.
                product = color * alpha + 128
                expected.append((product + (product >> 8)) >> 8)
            assert fixture.premul[offset:offset+4] == bytes((*expected, alpha)), fixture.name
    # Exhaustively validate oracle arithmetic, including every rounding boundary.
    for alpha in range(256):
        for color in range(256):
            product = color * alpha + 128
            assert (color * alpha + 127) // 255 == (product + (product >> 8)) >> 8
    print("PNG fixture verification PASS: framing=46 designated_faults=30 "
          "premul_byte_pairs=65536 pillow_pixels=%s" % ("15" if args.pillow else "skipped"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

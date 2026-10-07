PNG fixtures and independent pixel oracle
========================================

Run with Python 3.8 or newer; generation and default verification require only
the standard library:

```
python generate_png_fixtures.py
python generate_png_fixtures.py --check
python verify_png_fixtures.py
```

If Pillow is already installed, an independent host PNG decoder can additionally
check every conforming fixture:

```
python verify_png_fixtures.py --pillow
```

The generator writes `png_fixtures.h` and `png_fixture_manifest.json` in its own
directory. `--output-dir DIRECTORY` overrides the destination. `--check` performs
byte-for-byte comparisons without writing and exits nonzero for missing or stale
files. LF newlines and explicit integer encoders produce stable bytes across host
platforms and compressor versions. Python zlib only checks the independently
emitted zlib streams; it does not encode fixtures or derive expected pixels.

The header has 46 named fixtures: 15 conforming images, 30 malformed inputs, and
one structurally valid private critical extension requiring unknown semantics.
It contains 4,244 PNG bytes plus 1,720 expected pixel bytes, totaling 5,964 bytes.
Names, tables, pointers, alignment, and test code add separate linked-image cost;
this number does not claim that an entire executable fits an ELF loader limit.
The largest conforming image is 8x8 (64 pixels / 256 bytes per expected format).
All numeric metadata fields are `uint32_t`; expected pixels are packed rows in
top-to-bottom, left-to-right order with stride `width * 4`.

Use `gtos_png_fixtures` and `GTOS_PNG_FIXTURE_COUNT` to iterate. For conforming
fixtures, `expected_bgra` contains straight-alpha BGRA and
`expected_premul_rgba` contains premultiplied RGBA. Both have `expected_bytes`
bytes. The oracle reads source sample tuples, palette entries, and transparency
keys directly, without inspecting decoder output or encoded PNG bytes. Hidden
RGB under zero alpha remains in the straight-alpha oracle. Premultiplied color
uses exact nearest-integer `(color * alpha + 127) // 255`; alpha stays unchanged.
The verifier independently compares this arithmetic to the existing Skia shift
formula for every byte pair.

`GTOS_PNG_MALFORMED` and `GTOS_PNG_REQUIRES_EXTENSION` entries have no expected
pixel arrays and zero `expected_bytes`. Dimensions and format metadata on
negative entries are the declared IHDR values; they are not trusted geometry.
`first_deflate_block_type == UINT32_MAX` means unspecified. Positive values
0/1/2 identify stored/fixed/dynamic DEFLATE. `deflate_matches` records actual LZ77
length/distance pairs. The largest image/payload macros help size test buffers.

Conforming coverage:

- RGBA8 with asymmetric channels and alpha 0, 1, 127, 128, 254, 255.
- All PNG filters 0 through 4, including subtraction wraparound and neighboring
  rows. RGB8 also carries all filters.
- Genuine RFC 1950 zlib streams containing stored, fixed-Huffman, or
  dynamic-Huffman RFC 1951 blocks. Compressed fixtures contain real LZ77 matches.
  The handwritten complete dynamic trees make generation deterministic.
- RGB8 and grayscale8, both opaque and with transparency keys.
- Indexed8 with PLTE plus shortened tRNS, testing the remaining default alpha255.
- Grayscale-alpha8; odd-width packed grayscale1 and indexed2 plus transparency.
- RGBA16 using samples exactly representable in eight bits (multiples of 257).
- Adam7 with all seven passes in a 5x5 image and empty passes in a 1x1 image.
- Arbitrary consecutive IDAT splits, empty IDAT, split zlib header/checksum,
  and an unknown private ancillary chunk.

Malformed coverage includes bad signature, IHDR/IDAT CRC errors, chunk lengths
past input or above 2^31-1, truncated/missing IEND, missing IDAT, truncated zlib
and stored DEFLATE, bad Adler checksum, reserved DEFLATE block type, zero/high-bit
dimensions, illegal RGBA depth4, reserved color/compression/filter/interlace
values, invalid scanline filter5, too few/too many inflated scanline bytes,
duplicate IHDR, prohibited RGBA tRNS, missing/invalid PLTE, out-of-range palette
index, and too-long palette tRNS. Each claimed defect is independently checked
by the verifier. Invalid compression tests recompute PNG chunk CRCs so the
intended deeper failure is reachable.

Fixture classification describes the datastream, independent of a decoder's
supported profile. A conforming fixture must remain labeled conforming when a
component returns an explicit unsupported-format result. The private critical
`VpAg` extension is classified separately because unknown extension semantics
do not establish corruption. A capacity failure on a valid image is a resource
limit, not malformed input. No capacity-changing/kernel/ABI/VM edits are made.

Coverage limits: these fixtures do not qualify every PNG format or metadata
feature. They omit grayscale2/4/16, indexed1/4, RGB16, grayscale-alpha16, general
16-to-8 rounding, 16-bit transparency comparisons, color-management/gamma,
APNG animation, large dimensions, multi-block DEFLATE, preset dictionary errors,
all dynamic code-length run encodings, and all chunk ordering violations. Omitted
conforming features are not claimed corrupt. The alpha oracle does not perform
gamma/color-space conversion; PNGs contain no color-management metadata.

Primary references:

- [W3C PNG Third Edition, 24 June 2025](https://www.w3.org/TR/2025/REC-png-3-20250624/),
  sections 5.3-5.6 (framing, CRC, ordering), 6.2 (unassociated alpha), 7-9
  (sample packing, Adam7, filters), 10 (zlib), 11.2 (critical chunks),
  11.3.1.1 (tRNS), and 13.12 (sample rescaling).
- [RFC 1950](https://www.rfc-editor.org/rfc/rfc1950.html), zlib header/Adler32.
- [RFC 1951](https://www.rfc-editor.org/rfc/rfc1951.html), sections 3.1.1 and
  3.2.4-3.2.7 (bit order and stored/fixed/dynamic DEFLATE blocks).
- [Pinned upstream Skia SkMath.h](https://raw.githubusercontent.com/google/skia/00987348988a7355d6437a917fa153dde9ce6c82/include/private/SkMath.h)
  provides the component's `SkMulDiv255Round` comparison. The expected values
  use a separate division-based oracle.

Initial verification on the Windows host: generation/check PASS; designated
faults 30/30; all 65,536 premultiply pairs equal; installed Pillow independently
decoded all 15 conforming images and matched every expected channel byte.

Host component harness
----------------------

`host_tests.cc` includes the actual `png_decode.h` and `png_pixel.h` APIs. Link it
with the real PNG core archive, RGBA bridge, memory implementation, and active
Skia assertion-failure handler. It does not define decoder APIs or link stubs.
This is a host qualification program using `malloc`; it makes no native guest
execution claim.

The harness checks every fixture through BGRA and RGBA decode, every proper PNG
prefix, exact and padded capacities, byte-buffer alignment, context/result
alignment, null arguments, short strides, wrapping pointers, every prohibited
alias pair, spare-capacity overlap, adjacent regions, exact input EOF, and
context reuse across success/error/success. Caller-owned storage has guard bytes,
and bytes outside exact supplied capacities are separately checked. Every error
preserves the entire final RGBA allocation and requirements. Late errors with
successfully inspected geometry also check BGRA padding and unused work capacity
while allowing active scratch pixels/work to change.

The 2,000 deterministic mutation iterations include 500 checksum-repaired source
pixel mutations with independent raw-sample oracles. They cover every one of 32
RGBA source samples at every bit position (256 unique cases). Remaining seeded
mutations use xorshift, general bit changes, and selected IDAT CRC repairs to
reach deeper validation. Arbitrary mutations may succeed or fail; two runs must
agree, accepted pixels must obey the independent BGRA-to-RGBA arithmetic oracle,
and failures must preserve final output/result. Byte-level fixture mismatch
diagnostics keep source expectations unchanged when a component fails.

Actual component pass/fail evidence belongs to the root build logs. Fixture
generator/Pillow verification above does not substitute for running this harness
against the linked component.

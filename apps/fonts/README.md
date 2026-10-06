# GTOS Han: licensed compact Chinese raster font

GTOS Han is the renamed, bounded raster subset used by the GTOS desktop. It is
based on **Noto Sans CJK SC Regular, version 2.004**, by Adobe and Google, under
the SIL Open Font License 1.1. The complete font permission notice and the
upstream font's copyright attribution are in [OFL.txt](OFL.txt). Keep that notice
with every source or binary distribution containing this atlas. The original
Noto/Adobe names identify provenance and do not imply endorsement.

## Pinned source

- Official project: <https://github.com/notofonts/noto-cjk>
- Official release source: <https://github.com/notofonts/noto-cjk/blob/Sans2.004/Sans/OTC/NotoSansCJK-Regular.ttc>
- Official license: <https://github.com/notofonts/noto-cjk/blob/Sans2.004/LICENSE>
- Input: `NotoSansCJK-Regular.ttc`, face index **2**, `Noto Sans CJK SC`
- The exact source SHA-256, Pillow/FreeType/fontTools versions, atlas hash and
  complete Unicode coverage are recorded in [han_manifest.json](han_manifest.json)
- The generator rejects an unrecognized font hash, wrong face, absent `.notdef`
  mappings, empty glyphs, and any glyph extending outside its raster cell

The source TTC is installed by Debian's `fonts-noto-cjk` package at
`/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc`. No complete font, host
font library, shaping engine or font parser is loaded by the guest.

## Reproduction

From the repository root:

```
python3 tools/i18n_generate.py --font /path/to/NotoSansCJK-Regular.ttc
python3 tools/i18n_generate.py --check --font /path/to/NotoSansCJK-Regular.ttc
```

Use the versions in the manifest for byte-for-byte reproduction. The generation
algorithm is deterministic (sorted Unicode scalars, sorted dictionary keys,
explicit font face/layout engine/raster origin, integer alpha quantization).
Changing FreeType may change antialiasing; the checked-in raster remains the
kernel build input, and the regeneration check intentionally detects differences.
Normal builds and the mandatory coverage tests do not need these host packages.

## Format and scope

- Coverage is the union of every non-ASCII scalar in both catalog locales and
  all pinyin candidate phrases, plus U+25A1 WHITE SQUARE
- Body: 14 px font rasterized into a 16 × 18 cell; heading: separate 28 px
  rasterization into a 32 × 36 cell
- The drawing origin is `(0, -3 * scale)` with Pillow `anchor='la'`; this is
  already baked into pixels and fits fullwidth punctuation without cropping
- Alpha is row-major 4-bit coverage, high nibble first; row strides are 8/16 bytes
- Han advance is 14/28 px; punctuation uses its actual rounded source advance
- No runtime allocation; generated index, metrics and pixels are read-only
- U+FFFD is deliberately rendered using Noto's U+25A1 square because the source
  face has no U+FFFD. Unsupported characters are visibly marked, never replaced
  by an English label. Unsupported characters are not reported as covered
- Existing ASCII text remains in the separately licensed DejaVu desktop atlas

This is **bounded UI coverage**, not all Chinese characters, all Unicode,
font fallback, vector scaling or a shaping engine. Add text to the catalog or
candidate dictionary and regenerate before using a new Chinese glyph. The
compiled tests verify every displayed catalog and dictionary phrase.

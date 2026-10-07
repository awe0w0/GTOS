#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE__CORE
#define WUFFS_CONFIG__MODULE__BASE__INTERFACES
#define WUFFS_CONFIG__MODULE__BASE__PIXCONV
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__AVOID_CPU_ARCH
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#include WUFFS_INPUT
#include <stdio.h>

static uint32_t independent_crc(uint32_t state, const uint8_t* bytes, size_t len) {
  uint32_t value = state ^ UINT32_C(0xFFFFFFFF);
  for (size_t i = 0; i < len; ++i) {
    value ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      value = (value >> 1) ^ ((value & 1) ? UINT32_C(0xEDB88320) : 0);
    }
  }
  return value ^ UINT32_C(0xFFFFFFFF);
}

static uint32_t digest(uint32_t hash, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) hash = (hash ^ data[i]) * UINT32_C(16777619);
  return hash;
}

int main(void) {
  uint8_t source[8192];
  uint32_t random = UINT32_C(0x67128495);
  for (size_t i = 0; i < sizeof(source); ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    source[i] = (uint8_t)random;
  }
  unsigned crc_cases = 0;
  for (size_t n = 0; n <= sizeof(source); ++n) {
    wuffs_crc32__ieee_hasher hasher;
    if (wuffs_crc32__ieee_hasher__initialize(&hasher, sizeof(hasher), WUFFS_VERSION, 0).repr) return 2;
    const uint32_t expected = independent_crc(0, source, n);
    uint32_t actual = wuffs_crc32__ieee_hasher__update_u32(
        &hasher, wuffs_base__make_slice_u8(source, n));
    if (actual != expected) return 3;
    ++crc_cases;
    for (unsigned segmentation = 0; segmentation < 4; ++segmentation) {
      if (wuffs_crc32__ieee_hasher__initialize(&hasher, sizeof(hasher), WUFFS_VERSION, 0).repr) return 4;
      size_t cursor = 0;
      actual = 0;
      uint32_t rolling_expected = 0;
      while (cursor < n) {
        size_t length = (segmentation == 0) ? 1 :
                        (segmentation == 1) ? 15 :
                        (segmentation == 2) ? 32 : 1 + ((cursor * 71 + n) % 193);
        if (length > n - cursor) length = n - cursor;
        actual = wuffs_crc32__ieee_hasher__update_u32(
            &hasher, wuffs_base__make_slice_u8(source + cursor, length));
        rolling_expected = independent_crc(rolling_expected, source + cursor, length);
        if (actual != rolling_expected) return 5;
        /* Empty updates must preserve rolling state between real chunks. */
        if (wuffs_crc32__ieee_hasher__update_u32(
                &hasher, wuffs_base__make_slice_u8(source + cursor, 0)) != actual) return 6;
        cursor += length;
      }
      if (actual != expected) return 7;
      ++crc_cases;
    }
  }

  const uint32_t formats[] = {
    WUFFS_BASE__PIXEL_FORMAT__Y, WUFFS_BASE__PIXEL_FORMAT__Y_16BE,
    WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_NONPREMUL,
    WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_BINARY,
    WUFFS_BASE__PIXEL_FORMAT__BGR_565, WUFFS_BASE__PIXEL_FORMAT__BGR,
    WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
    WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL_4X16LE,
    WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL, WUFFS_BASE__PIXEL_FORMAT__BGRX,
    WUFFS_BASE__PIXEL_FORMAT__RGB, WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL,
    WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL,
  };
  const uint32_t destinations[] = {
    WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
    WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_NONPREMUL,
  };
  const size_t palette_lengths[] = {0, 16, 1023, 1024, 1032};
  unsigned pixel_cases = 0;
  for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
    for (unsigned d = 0; d < sizeof(destinations) / sizeof(destinations[0]); ++d) {
      for (unsigned pl = 0; pl < sizeof(palette_lengths) / sizeof(palette_lengths[0]); ++pl) {
        for (unsigned length = 0; length < 33; ++length) {
          uint8_t palette[1032], out[256];
          memset(palette, 0x7C, sizeof(palette));
          memset(out, 0xB5, sizeof(out));
          wuffs_base__pixel_swizzler swizzler;
          memset(&swizzler, 0x92, sizeof(swizzler));
          wuffs_base__status status = wuffs_base__pixel_swizzler__prepare(
              &swizzler, wuffs_base__make_pixel_format(destinations[d]),
              wuffs_base__make_slice_u8(palette, palette_lengths[pl]),
              wuffs_base__make_pixel_format(formats[f]),
              wuffs_base__make_slice_u8(source, 1024), WUFFS_BASE__PIXEL_BLEND__SRC);
          uint64_t n = wuffs_base__pixel_swizzler__swizzle_interleaved_from_slice(
              &swizzler, wuffs_base__make_slice_u8(out, sizeof(out)),
              wuffs_base__make_slice_u8(palette, palette_lengths[pl]),
              wuffs_base__make_slice_u8(source + 1024, length));
          uint32_t hash = digest(UINT32_C(2166136261), out, sizeof(out));
          hash = digest(hash, palette, sizeof(palette));
          printf("pixel %u %u %u %u %s %llu %08x\n", f, d, pl, length,
                 status.repr ? status.repr : "OK", (unsigned long long)n, hash);
          memset(out, 0x3E, sizeof(out));
          n = wuffs_base__pixel_swizzler__swizzle_interleaved_transparent_black(
              &swizzler, wuffs_base__make_slice_u8(out, sizeof(out)),
              wuffs_base__make_slice_u8(palette, palette_lengths[pl]), length);
          printf("black %llu %08x\n", (unsigned long long)n,
                 digest(UINT32_C(2166136261), out, sizeof(out)));
          ++pixel_cases;
        }
      }
    }
  }
  printf("CRC PASS %u rolling/segmentation cases; PIXEL %u differential cases; LAYOUT %zu %zu %zu %zu %zu\n",
         crc_cases, pixel_cases, sizeof(wuffs_adler32__hasher), sizeof(wuffs_crc32__ieee_hasher),
         sizeof(wuffs_deflate__decoder), sizeof(wuffs_zlib__decoder), sizeof(wuffs_png__decoder));
  return 0;
}

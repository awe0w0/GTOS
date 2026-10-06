// Actual upstream decoder; no OS calls or allocation in the reachable component.
#if defined(GTOS_NATIVE_COMPONENT) && (!defined(__i386__) || defined(__linux__))
#error A freestanding non-Linux i386 compiler is required for the GTOS app.
#endif
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE__CORE
#define WUFFS_CONFIG__MODULE__BASE__INTERFACES
#define WUFFS_CONFIG__MODULE__BASE__PIXCONV
#define WUFFS_CONFIG__MODULE__GIF
#define WUFFS_CONFIG__MODULE__LZW
#define WUFFS_CONFIG__AVOID_CPU_ARCH
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#if defined(__GNUC__) && !defined(__clang__)
// Selected upstream modules declare unused static interfaces from other modules.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "wuffs-v0.3.c"
#if defined(__GNUC__) && !defined(__clang__)
// GCC diagnoses these declarations at translation-unit end. The remaining
// adapter defines only exported functions and contains no unused statics.
#endif
#include "gif_codec.h"

size_t gtos_gif_context_size(void) { return sizeof(wuffs_gif__decoder); }

int gtos_gif_first_frame(void* storage, size_t storage_size,
                         const unsigned char* input, size_t input_size,
                         unsigned char* output, size_t output_size,
                         unsigned int* width, unsigned int* height) {
  if (!storage || storage_size < sizeof(wuffs_gif__decoder) || !input ||
      !output || !width || !height || ((uintptr_t)storage & 7)) return 1;
  wuffs_gif__decoder* decoder = storage;
  wuffs_base__status status = wuffs_gif__decoder__initialize(
      decoder, sizeof(*decoder), WUFFS_VERSION, 0);
  if (status.repr) return 2;
  wuffs_base__io_buffer source = wuffs_base__make_io_buffer(
      wuffs_base__make_slice_u8((uint8_t*)input, input_size),
      wuffs_base__make_io_buffer_meta(input_size, 0, 0, true));
  wuffs_base__image_config config = {0};
  status = wuffs_gif__decoder__decode_image_config(decoder, &config, &source);
  if (status.repr) return 3;
  uint32_t w = wuffs_base__pixel_config__width(&config.pixcfg);
  uint32_t h = wuffs_base__pixel_config__height(&config.pixcfg);
  if (!w || !h) return 4;
  size_t row = (size_t)w * 4;
  if (row / 4 != w) return 4;
  if (h > SIZE_MAX / row || (size_t)h * row > output_size) return 4;
  wuffs_base__pixel_config pixels = {0};
  wuffs_base__pixel_config__set(&pixels, WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
                               WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, w, h);
  wuffs_base__pixel_buffer buffer = {0};
  status = wuffs_base__pixel_buffer__set_from_slice(
      &buffer, &pixels, wuffs_base__make_slice_u8(output, output_size));
  if (status.repr) return 5;
  status = wuffs_gif__decoder__decode_frame(
      decoder, &buffer, &source, WUFFS_BASE__PIXEL_BLEND__SRC,
      wuffs_base__make_slice_u8(NULL, 0), NULL);
  if (status.repr) return 6;
  *width = w;
  *height = h;
  return 0;
}

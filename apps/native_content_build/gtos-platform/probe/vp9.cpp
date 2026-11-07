// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_record.h"

#include <cstdint>

#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

extern "C" {
extern const uint8_t gtos_vp9_fixture[];
int gtos_native_heap_enable_large_allocations() noexcept;

__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

constexpr unsigned kWidth = 320;
constexpr unsigned kHeight = 240;
constexpr unsigned kFirstPacket = 44;
constexpr unsigned kFirstSize = 3792;
constexpr unsigned kSecondPacket = kFirstPacket + kFirstSize + 12;
constexpr unsigned kSecondSize = 64;

bool Check(bool passed) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!passed) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
  return passed;
}

unsigned Decode(vpx_codec_ctx_t& decoder, unsigned offset, unsigned size) {
  if (!Check(vpx_codec_decode(&decoder, gtos_vp9_fixture + offset, size,
                             nullptr, 0) == VPX_CODEC_OK)) {
    return 0;
  }
  vpx_codec_iter_t frames = nullptr;
  const vpx_image_t* frame = vpx_codec_get_frame(&decoder, &frames);
  if (!Check(frame && frame->fmt == VPX_IMG_FMT_I420 &&
             frame->d_w == kWidth && frame->d_h == kHeight)) {
    return 0;
  }
  unsigned hash = 2166136261U;
  for (unsigned plane = 0; plane < 3; ++plane) {
    const unsigned width = plane ? kWidth / 2 : kWidth;
    const unsigned height = plane ? kHeight / 2 : kHeight;
    if (!Check(frame->planes[plane] &&
               frame->stride[plane] >= static_cast<int>(width))) {
      return 0;
    }
    for (unsigned y = 0; y < height; ++y) {
      const auto* row = frame->planes[plane] + y * frame->stride[plane];
      for (unsigned x = 0; x < width; ++x) {
        hash = (hash ^ row[x]) * 16777619U;
      }
    }
  }
  Check(vpx_codec_get_frame(&decoder, &frames) == nullptr);
  return hash;
}

}  // namespace

extern "C" int NativeEntry() {
  if (Check(gtos_native_heap_enable_large_allocations() == 0)) {
    vpx_codec_ctx_t decoder{};
    const vpx_codec_dec_cfg_t config{1, kWidth, kHeight};
    if (Check(vpx_codec_dec_init(&decoder, vpx_codec_vp9_dx(), &config, 0) ==
              VPX_CODEC_OK)) {
      const unsigned first = Decode(decoder, kFirstPacket, kFirstSize);
      const unsigned second = Decode(decoder, kSecondPacket, kSecondSize);
      native_memory_record.base = first;
      native_memory_record.fault_address = second;
#if defined(GTOS_VP9_GUEST_WRONG_EXPECTATION)
      Check(first && second && first == second);
#else
      Check(first && second && first != second);
#endif
      Check(vpx_codec_destroy(&decoder) == VPX_CODEC_OK);
    }

    vpx_codec_ctx_t truncated{};
    if (Check(vpx_codec_dec_init(&truncated, vpx_codec_vp9_dx(), &config, 0) ==
              VPX_CODEC_OK)) {
      Check(vpx_codec_decode(&truncated, gtos_vp9_fixture + kFirstPacket, 1,
                             nullptr, 0) != VPX_CODEC_OK);
      vpx_codec_iter_t frames = nullptr;
      Check(vpx_codec_get_frame(&truncated, &frames) == nullptr);
      Check(vpx_codec_destroy(&truncated) == VPX_CODEC_OK);
    }
  }
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

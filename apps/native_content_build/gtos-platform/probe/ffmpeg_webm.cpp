// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_record.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/mem.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

extern const uint8_t gtos_webm_fixture[];
extern const uint8_t gtos_vp9_reference[];
int gtos_native_heap_enable_large_allocations() noexcept;

__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

constexpr int kFixtureBytes = 67504;
constexpr unsigned kWidth = 320;
constexpr unsigned kHeight = 240;

bool Check(bool passed) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!passed)
    native_memory_record.failures = native_memory_record.failures + 1;
  return passed;
}

pthread_once_t once = PTHREAD_ONCE_INIT;
unsigned once_calls;

void InitializeOnce() {
  ++once_calls;
}

struct ThreadState {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condition{};
  bool ready = false;
  int status = -1;
};

void* Worker(void* argument) {
  auto* state = static_cast<ThreadState*>(argument);
  if (pthread_mutex_lock(&state->mutex) != 0)
    return nullptr;
  state->status = pthread_once(&once, InitializeOnce);
  state->ready = true;
  const int notified = pthread_cond_signal(&state->condition);
  const int unlocked = pthread_mutex_unlock(&state->mutex);
  return notified == 0 && unlocked == 0 ? argument : nullptr;
}

bool TestThread() {
  ThreadState state;
  pthread_t thread = 0;
  pthread_attr_t attr;
  Check(pthread_attr_init(nullptr) == EINVAL);
  if (!Check(pthread_attr_init(&attr) == 0))
    return false;
  const size_t default_stack = attr.__stacksize;
  Check(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN - 1) == EINVAL &&
        attr.__stacksize == default_stack);
  Check(pthread_attr_setstacksize(&attr, 16384) == 0);
  if (!Check(pthread_cond_init(&state.condition, nullptr) == 0) ||
      !Check(pthread_mutex_lock(&state.mutex) == 0) ||
      !Check(pthread_create(&thread, &attr, Worker, &state) == 0)) {
    return false;
  }
  int wait = 0;
  while (!state.ready && wait == 0)
    wait = pthread_cond_wait(&state.condition, &state.mutex);
  Check(wait == 0 && state.ready);
  Check(pthread_mutex_unlock(&state.mutex) == 0);
  void* result = nullptr;
  Check(pthread_join(thread, &result) == 0 && result == &state);
  Check(state.status == 0);
  Check(pthread_once(&once, InitializeOnce) == 0 && once_calls == 1);
  Check(pthread_join(thread, nullptr) == ESRCH);
  Check(pthread_cond_destroy(&state.condition) == 0);
  Check(pthread_mutex_destroy(&state.mutex) == 0);
  Check(pthread_attr_setstacksize(&attr, 1024 * 1024) == 0);
  thread = 0xabcdefU;
  Check(pthread_create(&thread, &attr, Worker, &state) == EINVAL &&
        thread == 0xabcdefU);
  Check(pthread_attr_destroy(&attr) == 0);
  Check(pthread_create(&thread, &attr, Worker, &state) == EINVAL &&
        thread == 0xabcdefU);
  Check(pthread_attr_destroy(&attr) == EINVAL);
  return true;
}

struct Cursor {
  int size;
  int position = 0;
};

int Read(void* opaque, uint8_t* output, int size) {
  auto& cursor = *static_cast<Cursor*>(opaque);
  if (cursor.position == cursor.size)
    return AVERROR_EOF;
  const int remaining = cursor.size - cursor.position;
  if (size > remaining)
    size = remaining;
  memcpy(output, gtos_webm_fixture + cursor.position, size);
  cursor.position += size;
  return size;
}

int64_t Seek(void* opaque, int64_t offset, int whence) {
  auto& cursor = *static_cast<Cursor*>(opaque);
  if (whence == AVSEEK_SIZE)
    return cursor.size;
  whence &= ~AVSEEK_FORCE;
  int base = 0;
  if (whence == SEEK_CUR)
    base = cursor.position;
  else if (whence == SEEK_END)
    base = cursor.size;
  else if (whence != SEEK_SET)
    return AVERROR(EINVAL);
  if (offset < -base || offset > cursor.size - base)
    return AVERROR(EINVAL);
  cursor.position = base + static_cast<int>(offset);
  return cursor.position;
}

class Demuxer {
 public:
  explicit Demuxer(int size) : cursor_{size} {}
  ~Demuxer() {
    if (opened_)
      avformat_close_input(&format_);
    else
      avformat_free_context(format_);
    if (io_) {
      av_freep(&io_->buffer);
      avio_context_free(&io_);
    }
  }

  int Open() {
    format_ = avformat_alloc_context();
    if (!format_)
      return AVERROR(ENOMEM);
    auto* buffer = static_cast<uint8_t*>(av_malloc(4096));
    if (!buffer)
      return AVERROR(ENOMEM);
    io_ = avio_alloc_context(buffer, 4096, 0, &cursor_, Read, nullptr, Seek);
    if (!io_) {
      av_free(buffer);
      return AVERROR(ENOMEM);
    }
    format_->pb = io_;
    format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    const int status = avformat_open_input(&format_, nullptr, nullptr, nullptr);
    opened_ = status == 0;
    return status;
  }

  AVFormatContext* get() const { return format_; }

 private:
  Cursor cursor_;
  AVFormatContext* format_ = nullptr;
  AVIOContext* io_ = nullptr;
  bool opened_ = false;
};

unsigned NextFrame(AVFormatContext* format, AVPacket* packet, int stream,
                   vpx_codec_ctx_t& decoder, unsigned offset, unsigned size) {
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    if (av_read_frame(format, packet) < 0)
      return 0;
    const bool video = packet->stream_index == stream;
    // The separate pinned IVF fixture contains the same encoded frames. Check
    // the real demuxer's packet boundaries and bytes before decoding them.
    if (video && !Check(packet->size == static_cast<int>(size) &&
                        memcmp(packet->data, gtos_vp9_reference + offset, size) == 0)) {
      av_packet_unref(packet);
      return 0;
    }
    const bool decoded = !video ||
        (packet->size > 0 && vpx_codec_decode(&decoder, packet->data,
                                             packet->size, nullptr, 0) == VPX_CODEC_OK);
    av_packet_unref(packet);
    if (!decoded)
      return 0;
    if (!video)
      continue;
    vpx_codec_iter_t frames = nullptr;
    const vpx_image_t* frame = vpx_codec_get_frame(&decoder, &frames);
    if (!frame)
      continue;
    if (frame->fmt != VPX_IMG_FMT_I420 || frame->d_w != kWidth || frame->d_h != kHeight)
      return 0;
    unsigned hash = 2166136261U;
    for (unsigned plane = 0; plane < 3; ++plane) {
      const unsigned width = plane ? kWidth / 2 : kWidth;
      const unsigned height = plane ? kHeight / 2 : kHeight;
      if (!frame->planes[plane] || frame->stride[plane] < static_cast<int>(width))
        return 0;
      for (unsigned y = 0; y < height; ++y) {
        const auto* row = frame->planes[plane] + y * frame->stride[plane];
        for (unsigned x = 0; x < width; ++x)
          hash = (hash ^ row[x]) * 16777619U;
      }
    }
    return hash;
  }
  return 0;
}

void TestWebM() {
  Demuxer input(kFixtureBytes);
  if (!Check(input.Open() == 0))
    return;
  int stream = -1;
  for (unsigned i = 0; i < input.get()->nb_streams; ++i) {
    const auto* codec = input.get()->streams[i]->codecpar;
    if (codec->codec_id == AV_CODEC_ID_VP9 && codec->width == kWidth &&
        codec->height == kHeight) {
      stream = static_cast<int>(i);
      break;
    }
  }
  if (!Check(stream >= 0))
    return;
  AVPacket* packet = av_packet_alloc();
  if (!Check(packet != nullptr))
    return;
  vpx_codec_ctx_t decoder{};
  const vpx_codec_dec_cfg_t config{1, kWidth, kHeight};
  if (Check(vpx_codec_dec_init(&decoder, vpx_codec_vp9_dx(), &config, 0) == VPX_CODEC_OK)) {
    const unsigned first = NextFrame(input.get(), packet, stream, decoder, 44, 3792);
    const unsigned second = NextFrame(input.get(), packet, stream, decoder, 3848, 64);
    Check(first != 0);
    Check(second != 0);
    native_memory_record.base = first;
    native_memory_record.fault_address = second;
#if defined(GTOS_FFMPEG_WEBM_GUEST_WRONG_EXPECTATION)
    Check(first == second);
#else
    Check(first != second);
#endif
    Check(vpx_codec_destroy(&decoder) == VPX_CODEC_OK);
  }
  av_packet_free(&packet);
  Demuxer truncated(8);
  const int status = truncated.Open();
  Check(status == AVERROR_INVALIDDATA || status == AVERROR_EOF);
}

}  // namespace

extern "C" int NativeEntry() {
  if (Check(gtos_native_heap_enable_large_allocations() == 0) && TestThread())
    TestWebM();
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

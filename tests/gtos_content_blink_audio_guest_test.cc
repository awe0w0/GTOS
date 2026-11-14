// Exercises Blink's SSE2 and scalar audio paths on a GTOS processor.
#include <algorithm>
#include <array>
#include <cstddef>

#include <process/abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "third_party/blink/renderer/platform/audio/vector_math.h"
#include "third_party/blink/renderer/platform/wtf/allocator/partitions.h"

namespace {

namespace math = blink::vector_math;
constexpr float kSentinel = -999.0f;

template <size_t Size>
bool Say(const char (&message)[Size]) {
  unsigned operation = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(message), "c"(Size - 1) : "cc", "memory");
  return operation == Size - 1;
}

bool CheckAlignmentAndTail() {
  alignas(32) std::array<float, 32> first{};
  alignas(32) std::array<float, 32> second{};
  alignas(32) std::array<float, 32> output{};
  for (size_t i = 0; i < first.size(); ++i) {
    first[i] = static_cast<int>(i % 13) - 6;
    second[i] = static_cast<int>(i % 5) + 2;
  }
  for (size_t offset = 0; offset < 8; ++offset) {
    const auto a = base::span(first).subspan(offset);
    const auto b = base::span(second).subspan(7 - offset);
    const auto result = base::span(output).subspan(offset);
    for (size_t count : {0u, 1u, 3u, 4u, 7u, 15u, 19u}) {
      output.fill(kSentinel);
      math::Vadd(a, b, result, count);
      for (size_t i = 0; i < count; ++i) {
        if (result[i] != a[i] + b[i]) {
          return false;
        }
      }
      const auto values = base::span(output);
      if (!std::ranges::all_of(values.first(offset),
                              [](float x) { return x == kSentinel; }) ||
          !std::ranges::all_of(result.subspan(count),
                              [](float x) { return x == kSentinel; })) {
        return false;
      }
      math::Vsma(a, -2.0f, result, count);
      float squares = 0.0f;
      float peak = 0.0f;
      for (size_t i = 0; i < count; ++i) {
        if (result[i] != b[i] - a[i]) {
          return false;
        }
        squares += a[i] * a[i];
        peak = std::max(peak, a[i] < 0.0f ? -a[i] : a[i]);
      }
      if (math::Vsvesq(a.first(count)) != squares ||
          math::Vmaxmgv(a.first(count)) != peak) {
        return false;
      }
    }
  }
  return true;
}

bool CheckConvolution() {
  constexpr std::array<float, 8> coefficients = {1, -2, 3, 4, 0, -1, 1, 2};
  alignas(32) std::array<float, 26> samples{};
  alignas(32) std::array<float, 16> output{};
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<int>(i % 9) - 4;
  }
  for (size_t taps : {3u, 4u, 8u}) {
    const auto filter = base::span(coefficients).first(taps);
    blink::AudioFloatArray prepared;
    math::PrepareFilterForConv(filter, prepared);
    if (prepared.as_span().empty()) {
      return false;
    }
    constexpr size_t frames = 13;
    output.fill(kSentinel);
    math::Conv(samples, filter, output, frames, prepared);
    for (size_t out = 0; out < frames; ++out) {
      float expected = 0.0f;
      for (size_t tap = 0; tap < taps; ++tap) {
        expected += samples[out + tap] * filter[taps - 1 - tap];
      }
      if (output[out] != expected) {
        return false;
      }
    }
    if (!std::ranges::all_of(base::span(output).subspan(frames),
                            [](float x) { return x == kSentinel; })) {
      return false;
    }
  }
  return true;
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  blink::Partitions::Initialize();
  if (!CheckAlignmentAndTail() ||
      !Say("GTOS BLINK AUDIO ALIGNMENT AND TAIL PASS\n")) {
    return 201;
  }
  if (!CheckConvolution() || !Say("GTOS BLINK AUDIO CONVOLUTION PASS\n")) {
    return 202;
  }
  return 37;
}

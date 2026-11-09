// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>

#include "common_audio/fir_filter.h"
#include "common_audio/fir_filter_factory.h"
#include "modules/audio_processing/aec3/fft_data.h"
#include "modules/audio_processing/aec3/vector_math.h"
#include "modules/audio_processing/agc2/rnn_vad/vector_math.h"

#if defined(WEBRTC_ENABLE_AVX2)
#error The GTOS audio probe uses the scalar and SSE2 profile.
#endif

int TestWebRtcAudioIsa() {
    using webrtc::Aec3Optimization;
    const auto optimization = webrtc::DetectOptimization();
    if (optimization != Aec3Optimization::kSse2) return 19;

    webrtc::FftData fft;
    for (size_t i = 0; i < fft.re.size(); ++i) {
        fft.re[i] = static_cast<float>(i);
        fft.im[i] = 2;
    }
    std::array<float, webrtc::kFftLengthBy2Plus1> spectrum{};
    for (auto mode : {optimization, Aec3Optimization::kAvx2}) {
        fft.Spectrum(mode, spectrum);
        for (size_t i = 0; i < spectrum.size(); ++i)
            if (spectrum[i] != static_cast<float>(i * i + 4)) return 20;
    }

    const std::array<float, 7> input{1, 2, 3, 4, 5, 6, 7};
    std::array<float, 7> output{};
    for (auto mode : {optimization, Aec3Optimization::kAvx2}) {
        webrtc::VectorMath math(mode);
        math.Multiply(input, input, output);
        math.Sqrt(output);
        math.Accumulate(input, output);
        for (size_t i = 0; i < output.size(); ++i)
            if (output[i] != 2 * input[i]) return 21;
    }
    const webrtc::rnn_vad::VectorMath dot_product({true, true, false});
    if (dot_product.DotProduct(input, input) != 140) return 22;

    const float coefficients[] = {.25f, .75f};
    const float impulse[] = {1, 0, 0, 0};
    std::unique_ptr<webrtc::FIRFilter> filter(
        webrtc::CreateFirFilter(coefficients, 2, 4));
    if (!filter) return 23;
    std::array<float, 4> filtered{};
    filter->Filter(impulse, 4, filtered.data());
    if (filtered != std::array<float, 4>{.25f, .75f, 0, 0}) return 23;
    return 0;
}

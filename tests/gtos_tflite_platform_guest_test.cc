// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include "third_party/gemmlowp/src/public/gemmlowp.h"
#include "third_party/tflite/src/tensorflow/lite/kernels/random_ops.cc"
#include "tools/gtos_native_sdk/native-services.h"

namespace {

struct CountTask : gemmlowp::Task {
    void Run() override {
        thread_id = gtos::native::CurrentProcessInfo().thread_id;
        ++calls;
    }

    unsigned calls = 0;
    uint32_t thread_id = 0;
};

bool TestWorkers() {
    const auto process = gtos::native::CurrentProcessInfo();
    if (gemmlowp::GetHardwareConcurrency(0) != process.scheduler_cpu_count ||
        gemmlowp::GetHardwareConcurrency(2) != 2) return false;
    CountTask tasks[2];
    for (unsigned round = 0; round < 3; ++round) {
        gemmlowp::WorkersPool pool;
        for (unsigned repeat = 0; repeat < 4; ++repeat)
            pool.Execute(2, tasks);
        if (!tasks[0].thread_id || tasks[0].thread_id == process.thread_id ||
            tasks[1].thread_id != process.thread_id ||
            tasks[0].calls != (round + 1) * 4 || tasks[1].calls != tasks[0].calls)
            return false;
    }
    return true;
}

bool TestSeeds() {
    using namespace tflite::ops::builtin::random;
    auto generate = [](TfLiteRandomParams params) {
        OpData data;
        TfLiteNode node{};
        node.builtin_data = &params;
        node.user_data = &data;
        InitializeOpData(&node);
        return data.rng();
    };
    auto equal = [](const auto& a, const auto& b) {
        for (unsigned i = 0; i < Generator::kResultElementCount; ++i)
            if (a[i] != b[i]) return false;
        return true;
    };
    Generator specified(7, 0);
    Generator zero(0, 0);
    if (!equal(generate({7, 0}), specified())) return false;
    const auto first = generate({0, 0});
    const auto second = generate({0, 0});
    return !equal(first, second) && !equal(first, zero());
}

bool TestSleep() {
    timespec start{}, end{};
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (clock_gettime(CLOCK_MONOTONIC, &end)) return false;
    return (end.tv_sec - start.tv_sec) * 1000000000LL + end.tv_nsec - start.tv_nsec >= 1000000;
}

} // namespace

int TestTflitePlatform() {
    if (!TestWorkers()) return 12;
    if (!TestSeeds()) return 13;
    if (!TestSleep()) return 14;
    return 0;
}

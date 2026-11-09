// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <exception>
#include <future>
#include <thread>
#include <utility>

#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"

int TestLibcppThread() {
    const auto process = gtos::native::CurrentProcessInfo();
    if (!process.scheduler_cpu_count ||
        std::thread::hardware_concurrency() != process.scheduler_cpu_count)
        return 40;
    const auto empty = std::current_exception();
    const std::exception_ptr copy = empty;
    if (copy || gtos_libcpp_tls_get(0) ||
        gtos_libcpp_tls_create(nullptr, nullptr) == 0 ||
        gtos_libcpp_tls_set(0, nullptr) == 0)
        return 46;

    unsigned worker_id = 0;
    std::promise<void> promise;
    auto completed = promise.get_future();
    std::thread worker([&] {
        worker_id = gtos::native::CurrentProcessInfo().thread_id;
        promise.set_value_at_thread_exit();
    });
    std::thread moved = std::move(worker);
    if (worker.joinable() || !moved.joinable()) return 41;
    completed.get();
    moved.join();
    if (moved.joinable() || !worker_id || worker_id == process.thread_id)
        return 42;

    std::atomic<bool> ran{false};
    std::thread detached([&] { ran.store(true, std::memory_order_release); });
    detached.detach();
    if (detached.joinable()) return 43;
    unsigned collected = 0;
    for (unsigned attempt = 0; attempt < 256 && !collected; ++attempt) {
        std::this_thread::yield();
        collected = gtos::native::CollectDetachedThreads();
    }
    if (collected != 1 || !ran.load(std::memory_order_acquire)) return 44;

    unsigned reused = 0;
    std::thread next([&] { reused = gtos::native::CurrentProcessInfo().thread_id; });
    next.join();
    if (reused <= worker_id || next.joinable()) return 45;
    return 0;
}

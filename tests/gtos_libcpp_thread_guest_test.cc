// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

#include <process/thread_abi.h>
#include <process/thread_slot_abi.h>

#include "third_party/blink/renderer/platform/wtf/stack_util.h"
#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"
#include "v8/src/base/platform/platform.h"

namespace {

bool BlinkStackMatches(const GtosProcessInfoResult& info) {
    static int outside_stack;
    int local = 0;
    const uintptr_t position = blink::GetCurrentStackPosition();
    return reinterpret_cast<uintptr_t>(blink::GetStackStart()) == info.stack_end &&
           blink::GetUnderestimatedStackSize() == info.stack_end - info.stack_begin &&
           position >= info.stack_begin && position < info.stack_end &&
           blink::IsOnStack(&local) && !blink::IsOnStack(&outside_stack);
}

struct ThreadGate {
    std::mutex mutex;
    std::condition_variable ready;
    unsigned entered = 0;
    bool released = false;
};

void* WaitAtGate(void* argument) {
    auto& gate = *static_cast<ThreadGate*>(argument);
    const auto thread = gtos::native::CurrentProcessInfo();
    static thread_local unsigned local_id;
    GtosThreadSlotInfo slot{};
    const GtosThreadSlotRequest request{
        GTOS_THREAD_SLOT_ABI_VERSION, reinterpret_cast<unsigned>(&slot)};
    unsigned operation = GTOS_SYS_THREAD_SLOT_INFO;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
    if (operation || slot.version != GTOS_THREAD_SLOT_ABI_VERSION ||
        slot.index >= thread.maximum_parallel_threads ||
        slot.thread_id != thread.thread_id || local_id != 0)
        return nullptr;
    local_id = thread.thread_id;
    errno = static_cast<int>(thread.thread_id);
    std::unique_lock lock(gate.mutex);
    ++gate.entered;
    gate.ready.wait(lock, [&] { return gate.released; });
    if (local_id != thread.thread_id || errno != static_cast<int>(thread.thread_id))
        return nullptr;
    return reinterpret_cast<void*>(thread.thread_id);
}

int TestThreadCapacity(const GtosProcessInfoResult& process) {
    const unsigned limit = process.maximum_parallel_threads;
#if defined(GTOS_EXPECT_CONTENT_THREADS)
    if (limit != GTOS_CONTENT_THREAD_SLOT_LIMIT) return 47;
#else
    if (limit != GTOS_THREAD_SLOT_LIMIT) return 47;
#endif
    ThreadGate gate;
    std::array<unsigned, GTOS_CONTENT_THREAD_SLOT_LIMIT - 1> ids{};
    unsigned count = 0;
    int status = 0;
    for (; count < limit - 1; ++count) {
        if (gtos::native::StartThread(0, WaitAtGate, &gate, &ids[count])) {
            status = 48;
            break;
        }
    }
    bool all_waiting = false;
    for (unsigned attempt = 0; attempt < 4096 && !all_waiting; ++attempt) {
        {
            std::lock_guard lock(gate.mutex);
            all_waiting = gate.entered == count;
        }
        std::this_thread::yield();
    }
    if (!status && !all_waiting) status = 49;

    unsigned extra = 0;
    if (!status && (gtos::native::StartThread(0, WaitAtGate, &gate, &extra) !=
                        -GTOS_THREAD_ERR_LIMIT || extra != 0))
        status = 50;

    {
        std::lock_guard lock(gate.mutex);
        gate.released = true;
        gate.ready.notify_all();
    }
    if (extra && gtos::native::JoinThread(extra)) status = 51;
    for (unsigned i = 0; i < count; ++i) {
        void* result = nullptr;
        if (gtos::native::JoinThread(ids[i], &result) ||
            reinterpret_cast<uintptr_t>(result) != ids[i])
            status = 51;
    }
    if (!status) {
        unsigned id = 0;
        void* result = nullptr;
        if (gtos::native::StartThread(0, WaitAtGate, &gate, &id) ||
            id <= ids[count - 1] || gtos::native::JoinThread(id, &result) ||
            reinterpret_cast<uintptr_t>(result) != id)
            status = 54;
    }
    return status;
}

struct StaticInitialization {
    std::atomic<unsigned> requests{0};
    std::atomic<unsigned> initializers{0};
    std::atomic<unsigned> completed{0};
    std::atomic<bool> release{false};
};

int GuardedValue(StaticInitialization& state) {
    static const int value = [&] {
        ++state.initializers;
        while (!state.release.load()) std::this_thread::yield();
        return 42;
    }();
    return value;
}

void* ReadGuardedValue(void* argument) {
    auto& state = *static_cast<StaticInitialization*>(argument);
    ++state.requests;
    const int value = GuardedValue(state);
    ++state.completed;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(value));
}

bool WaitForCount(const std::atomic<unsigned>& count, unsigned expected) {
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        if (count.load() == expected) return true;
        std::this_thread::yield();
    }
    return false;
}

int TestStaticInitialization() {
    StaticInitialization state;
    unsigned initializer = 0;
    unsigned contender = 0;
    if (gtos::native::StartThread(0, ReadGuardedValue, &state, &initializer)) return 55;

    int status = 0;
    if (!WaitForCount(state.initializers, 1) ||
        gtos::native::StartThread(0, ReadGuardedValue, &state, &contender)) {
        status = 56;
    } else if (!WaitForCount(state.requests, 2)) {
        status = 57;
    } else {
        // Let the contender reach the guard while the initializer is still pending.
        for (unsigned attempt = 0; attempt < 64; ++attempt) std::this_thread::yield();
        if (state.completed.load() || state.initializers.load() != 1) status = 58;
    }

    state.release.store(true);
    for (unsigned thread : {initializer, contender}) {
        if (!thread) continue;
        void* result = nullptr;
        if (gtos::native::JoinThread(thread, &result) ||
            reinterpret_cast<uintptr_t>(result) != 42)
            return 59;
    }
    if (!status && (state.completed.load() != 2 || state.initializers.load() != 1))
        status = 59;
    return status;
}

class V8Worker : public v8::base::Thread {
public:
    V8Worker() : Thread(Options("gtos-v8-worker")) {}
    unsigned id = 0;

private:
    void Run() override {
        id = gtos::native::CurrentProcessInfo().thread_id;
    }
};

}  // namespace

int TestLibcppThread() {
    const auto process = gtos::native::CurrentProcessInfo();
    blink::internal::InitializeMainThreadStackEstimate();
    if (!BlinkStackMatches(process) || blink::MayNotBeMainThread()) return 96;
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
    bool worker_stack_valid = false;
    std::promise<void> promise;
    auto completed = promise.get_future();
    std::thread worker([&] {
        const auto info = gtos::native::CurrentProcessInfo();
        worker_id = info.thread_id;
        worker_stack_valid = BlinkStackMatches(info) && blink::MayNotBeMainThread();
        promise.set_value_at_thread_exit();
    });
    std::thread moved = std::move(worker);
    if (worker.joinable() || !moved.joinable()) return 41;
    completed.get();
    moved.join();
    if (moved.joinable() || !worker_id || worker_id == process.thread_id)
        return 42;
    if (!worker_stack_valid || !BlinkStackMatches(process)) return 97;

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
    if (const int status = TestThreadCapacity(process)) return status;
    if (const int status = TestStaticInitialization()) return status;

    V8Worker v8_worker;
    if (!v8_worker.StartSynchronously()) return 52;
    v8_worker.Join();
    if (v8_worker.id <= reused) return 53;
    return 0;
}

// Copyright 2026 The BoringSSL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define _BORINGSSL_PROHIBIT_OPENSSL_MALLOC
#include "internal.h"

#if defined(OPENSSL_GTOS_THREADS)

#include <process/abi.h>
#include <process/info_abi.h>

extern "C" int __cxa_thread_atexit(void (*)(void *), void *, void *) noexcept;
extern "C" void *__dso_handle;

BSSL_NAMESPACE_BEGIN
namespace {

GtosProcessInfoResult CurrentThread() {
  GtosProcessInfoResult result = {};
  GtosProcessInfoRequest request = {
      GTOS_PROCESS_INFO_ABI_VERSION, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result)), sizeof(result)};
  unsigned call = GTOS_SYS_PROCESS_INFO;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(&request))),
                 "c"(sizeof(request))
               : "memory", "cc");
  BSSL_CHECK(call == 0 && result.version == GTOS_PROCESS_INFO_ABI_VERSION &&
             result.process_id > 0 && result.process_id <= 0x7fffffffU &&
             result.thread_id > 0 && result.thread_id <= 0x7fffffffU &&
             result.user_begin == 0x40000000U && result.user_end == 0xc0000000U &&
             result.stack_begin >= result.user_begin &&
             result.stack_begin < result.stack_end &&
             result.stack_end <= result.user_end &&
             result.stack_end - result.stack_begin >= 8192 &&
             ((result.stack_begin | result.stack_end) & 4095) == 0 &&
             result.page_bytes == 4096 && result.maximum_pages >= 2 &&
             result.maximum_regions > 0 && result.scheduler_cpu_count == 1 &&
             (result.maximum_parallel_threads == 1 ||
              result.maximum_parallel_threads == 4) &&
             (result.maximum_parallel_threads != 1 ||
              result.thread_id == result.process_id));
  return result;
}

void Yield(const GtosProcessInfoResult &info) {
  BSSL_CHECK(info.maximum_parallel_threads == 4);
  unsigned call = GTOS_SYS_YIELD;
  asm volatile("int $0x80" : "+a"(call) : "b"(0U), "c"(0U) : "memory", "cc");
  BSSL_CHECK(call == 0);
}

class GateGuard {
 public:
  GateGuard(std::atomic<uint32_t> &gate, const GtosProcessInfoResult &info)
      : gate_(gate) {
    uint32_t expected = 0;
    while (!gate_.compare_exchange_strong(expected, info.thread_id,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
      BSSL_CHECK(expected != info.thread_id);
      Yield(info);
      expected = 0;
    }
  }
  ~GateGuard() { gate_.store(0, std::memory_order_release); }
  GateGuard(const GateGuard &) = delete;
  GateGuard &operator=(const GateGuard &) = delete;

 private:
  std::atomic<uint32_t> &gate_;
};

bool HasReaders(const GtosReadWriteLock &lock) {
  bool result = false;
  for (const auto &reader : lock.readers) {
    BSSL_CHECK((reader.owner == 0) == (reader.depth == 0));
    result |= reader.depth != 0;
  }
  return result;
}

struct ThreadLocalState {
  uint32_t owner = 0;
  bool registered = false;
  void *values[NUM_OPENSSL_THREAD_LOCALS] = {};
};

std::atomic<uint32_t> g_thread_local_gate{0};
ThreadLocalState g_thread_locals[4];
thread_local_destructor_t g_destructors[NUM_OPENSSL_THREAD_LOCALS] = {};

void ThreadLocalCleanup(void *arg) {
  auto *state = static_cast<ThreadLocalState *>(arg);
  const auto info = CurrentThread();
  void *values[NUM_OPENSSL_THREAD_LOCALS];
  thread_local_destructor_t destructors[NUM_OPENSSL_THREAD_LOCALS];
  {
    GateGuard guard(g_thread_local_gate, info);
    BSSL_CHECK(state->owner == info.thread_id && state->registered);
    for (unsigned i = 0; i < NUM_OPENSSL_THREAD_LOCALS; i++) {
      values[i] = state->values[i];
      destructors[i] = g_destructors[i];
    }
    // Detach before callbacks. Reentrant setters get a new cleanup entry, and
    // this callback must not touch the reusable state after releasing the gate.
    *state = {};
  }
  for (unsigned i = 0; i < NUM_OPENSSL_THREAD_LOCALS; i++) {
    if (destructors[i] != nullptr) {
      destructors[i](values[i]);
    }
  }
}

}  // namespace

void StaticMutex::LockRead() {
  const auto info = CurrentThread();
  for (;;) {
    {
      GateGuard guard(lock_.gate, info);
      BSSL_CHECK(lock_.writer != info.thread_id);
      HasReaders(lock_);
      GtosReadWriteLock::Reader *available = nullptr;
      for (auto &reader : lock_.readers) {
        if (reader.owner == info.thread_id) {
          BSSL_CHECK(lock_.writer == 0 && reader.depth < UINT32_MAX);
          reader.depth++;
          return;
        }
        if (reader.owner == 0) {
          available = &reader;
        }
      }
      if (lock_.writer == 0 && lock_.next_ticket == lock_.serving_ticket) {
        BSSL_CHECK(available != nullptr);
        available->owner = info.thread_id;
        available->depth = 1;
        return;
      }
    }
    Yield(info);
  }
}

void StaticMutex::UnlockRead() {
  const auto info = CurrentThread();
  GateGuard guard(lock_.gate, info);
  BSSL_CHECK(lock_.writer == 0);
  HasReaders(lock_);
  for (auto &reader : lock_.readers) {
    if (reader.owner == info.thread_id) {
      if (--reader.depth == 0) {
        reader.owner = 0;
      }
      return;
    }
  }
  BSSL_CHECK(false);
}

void StaticMutex::LockWrite() {
  const auto info = CurrentThread();
  uint32_t ticket;
  {
    GateGuard guard(lock_.gate, info);
    BSSL_CHECK(lock_.writer != info.thread_id);
    HasReaders(lock_);
    for (const auto &reader : lock_.readers) {
      BSSL_CHECK(reader.owner != info.thread_id);
    }
    BSSL_CHECK(lock_.next_ticket - lock_.serving_ticket < 4);
    ticket = lock_.next_ticket++;
  }
  for (;;) {
    {
      GateGuard guard(lock_.gate, info);
      if (lock_.writer == 0 && !HasReaders(lock_) &&
          ticket == lock_.serving_ticket) {
        lock_.writer = info.thread_id;
        return;
      }
    }
    Yield(info);
  }
}

void StaticMutex::UnlockWrite() {
  const auto info = CurrentThread();
  GateGuard guard(lock_.gate, info);
  BSSL_CHECK(lock_.writer == info.thread_id && !HasReaders(lock_) &&
             lock_.next_ticket != lock_.serving_ticket);
  lock_.writer = 0;
  lock_.serving_ticket++;
}

Mutex::~Mutex() {
  const auto info = CurrentThread();
  GateGuard guard(lock_.gate, info);
  BSSL_CHECK(lock_.writer == 0 && !HasReaders(lock_) &&
             lock_.next_ticket == lock_.serving_ticket);
}

void CRYPTO_once(CRYPTO_once_t *once, void (*init)()) {
  BSSL_CHECK(once != nullptr && init != nullptr);
  const auto info = CurrentThread();
  const uint32_t pending = 0x80000000U | info.thread_id;
  for (;;) {
    uint32_t state = once->load(std::memory_order_acquire);
    if (state == 2) {
      return;
    }
    BSSL_CHECK(state == 0 || (state > 0x80000000U && state != pending));
    if (state == 0 && once->compare_exchange_strong(
                          state, pending, std::memory_order_acquire,
                          std::memory_order_relaxed)) {
      init();
      once->store(2, std::memory_order_release);
      return;
    }
    Yield(info);
  }
}

void *CRYPTO_get_thread_local(thread_local_data_t index) {
  BSSL_CHECK(static_cast<unsigned>(index) < NUM_OPENSSL_THREAD_LOCALS);
  const auto info = CurrentThread();
  GateGuard guard(g_thread_local_gate, info);
  for (const auto &state : g_thread_locals) {
    if (state.owner == info.thread_id) {
      return state.values[index];
    }
  }
  return nullptr;
}

int CRYPTO_set_thread_local(thread_local_data_t index, void *value,
                            thread_local_destructor_t destructor) {
  BSSL_CHECK(static_cast<unsigned>(index) < NUM_OPENSSL_THREAD_LOCALS);
  const auto info = CurrentThread();
  ThreadLocalState *state = nullptr;
  {
    GateGuard guard(g_thread_local_gate, info);
    for (auto &item : g_thread_locals) {
      if (item.owner == info.thread_id) {
        if (item.registered) {
          g_destructors[index] = destructor;
          item.values[index] = value;
          return 1;
        }
        // An allocation-time reentry must not publish into an unregistered
        // state or interfere with the outer registration's ownership.
        state = &item;
        break;
      }
    }
    if (state == nullptr) {
      for (auto &item : g_thread_locals) {
        if (item.owner == 0) {
          item.owner = info.thread_id;
          state = &item;
          break;
        }
      }
    } else {
      state = nullptr;
    }
  }
  if (state == nullptr) {
    destructor(value);
    return 0;
  }
  if (__cxa_thread_atexit(ThreadLocalCleanup, state, &__dso_handle) != 0) {
    {
      GateGuard guard(g_thread_local_gate, info);
      BSSL_CHECK(state->owner == info.thread_id && !state->registered);
      *state = {};
    }
    destructor(value);
    return 0;
  }
  {
    GateGuard guard(g_thread_local_gate, info);
    BSSL_CHECK(state->owner == info.thread_id && !state->registered);
    state->registered = true;
    g_destructors[index] = destructor;
    state->values[index] = value;
  }
  return 1;
}

BSSL_NAMESPACE_END

#endif  // OPENSSL_GTOS_THREADS

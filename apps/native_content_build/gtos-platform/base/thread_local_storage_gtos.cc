// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/thread_local_storage.h"

#include <array>
#include <atomic>
#include <limits>

#include <process/thread_slot_abi.h>

#include "base/check_op.h"

extern "C" int __cxa_thread_atexit(void (*)(void*), void*, void*) noexcept;
extern "C" void* __dso_handle;

namespace base::internal {
namespace {

using Key = PlatformThreadLocalStorage::TLSKey;
constexpr unsigned kIndexBits = 4;
constexpr Key kSlots = 1U << kIndexBits;
constexpr Key kIndexMask = kSlots - 1;
constexpr Key kGenerationLimit = std::numeric_limits<Key>::max() >> kIndexBits;

// Even generations are vacant, odd generations are live. Never wrap a slot
// back to an identity a thread could still have stored.
constinit std::array<std::atomic<Key>, kSlots> generations{};

struct Cell {
  Key key;
  uintptr_t bits;  // Chromium encodes TLS state in the low bits of this value.
};
struct ThreadValues {
  std::array<Cell, kSlots> cells;
  uint32_t owner;
  bool registered;
};
constinit std::array<ThreadValues, GTOS_CONTENT_THREAD_SLOT_LIMIT> thread_values{};

ThreadValues& CurrentValues() {
  // This lookup also runs in allocator and lock instrumentation. Compiler
  // emulated TLS would allocate here and could reenter those callers.
  const auto info = ::gtos::native::CurrentThreadSlot();
  ThreadValues& values = thread_values[info.index];
  if (values.owner != info.thread_id) {
    values = {};
    values.owner = info.thread_id;
  }
  return values;
}

bool LiveKey(Key key) {
  const Key generation = key >> kIndexBits;
  return (generation & 1U) &&
         generations[key & kIndexMask].load(std::memory_order_acquire) ==
             generation;
}

void DestroyBaseTls(void*) {
  PlatformThreadLocalStorage::OnThreadExit();
}

}  // namespace

bool PlatformThreadLocalStorage::AllocTLS(TLSKey* key) {
  for (Key slot = 0; slot < kSlots; ++slot) {
    Key generation = generations[slot].load(std::memory_order_relaxed);
    if ((generation & 1U) || generation >= kGenerationLimit - 1) {
      continue;
    }
    if (generations[slot].compare_exchange_strong(
            generation, generation + 1, std::memory_order_acq_rel)) {
      *key = ((generation + 1) << kIndexBits) | slot;
      return true;
    }
  }
  return false;
}

void PlatformThreadLocalStorage::FreeTLS(TLSKey key) {
  CHECK(LiveKey(key));
  const Key generation = key >> kIndexBits;
  generations[key & kIndexMask].store(generation + 1, std::memory_order_release);
}

void* PlatformThreadLocalStorage::GetTLSValue(TLSKey key) {
  if (!LiveKey(key)) {
    return nullptr;
  }
  const Cell& cell = CurrentValues().cells[key & kIndexMask];
  return cell.key == key ? reinterpret_cast<void*>(cell.bits) : nullptr;
}

void PlatformThreadLocalStorage::SetTLSValue(TLSKey key, void* value) {
  CHECK(LiveKey(key));
  ThreadValues& values = CurrentValues();
  values.cells[key & kIndexMask] = {key, reinterpret_cast<uintptr_t>(value)};
  if (value && !values.registered) {
    // Publish the value before registering so allocator reentry can see it.
    values.registered = true;
    CHECK_EQ(__cxa_thread_atexit(DestroyBaseTls, nullptr, &__dso_handle), 0);
  }
}

}  // namespace base::internal

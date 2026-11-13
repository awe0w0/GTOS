// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/synchronization/waitable_event.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

#include <process/clock_abi.h>
#include <process/event_abi.h>

#include "base/check_op.h"
#include "base/time/time.h"
#include "base/time/time_override.h"

namespace base {
namespace {

template <class Request>
int EventCall(unsigned operation, const Request& request) {
  asm volatile("int $0x80"
               : "+a"(operation)
               : "b"(&request), "c"(sizeof(request))
               : "memory", "cc");
  return static_cast<int>(operation);
}

unsigned CreateEvent(WaitableEvent::ResetPolicy reset,
                     WaitableEvent::InitialState initial) {
  const unsigned flags =
      (reset == WaitableEvent::ResetPolicy::MANUAL ? GTOS_EVENT_MANUAL_RESET : 0) |
      (initial == WaitableEvent::InitialState::SIGNALED ? GTOS_EVENT_INITIAL_SIGNAL : 0);
  unsigned handle = 0;
  const GtosEventCreateRequest request{GTOS_EVENT_ABI_VERSION, flags,
      reinterpret_cast<unsigned>(&handle)};
  CHECK_EQ(EventCall(GTOS_SYS_EVENT_CREATE, request), 0);
  CHECK_NE(handle, 0u);
  return handle;
}

void ControlEvent(unsigned operation, unsigned handle) {
  const GtosEventControlRequest request{GTOS_EVENT_ABI_VERSION, handle};
  CHECK_EQ(EventCall(operation, request), 0);
}

std::optional<size_t> WaitOnce(span<const unsigned> handles, unsigned ticks) {
  const GtosEventWaitRequest request{GTOS_EVENT_ABI_VERSION,
      reinterpret_cast<unsigned>(handles.data()),
      static_cast<unsigned>(handles.size()), ticks};
  const int result = EventCall(GTOS_SYS_EVENT_WAIT, request);
  if (result == GTOS_EVENT_ERR_TIMEOUT) {
    return std::nullopt;
  }
  CHECK_GE(result, 0);
  CHECK_LT(static_cast<size_t>(result), handles.size());
  return static_cast<size_t>(result);
}

unsigned TimeoutTicks(TimeDelta remaining) {
  constexpr int64_t kMaxIntervalUs = std::numeric_limits<int>::max() * 1000LL;
  constexpr uint64_t kDenominator = 1000000ULL * GTOS_CLOCK_PIT_DIVISOR;
  static_assert((kMaxIntervalUs * GTOS_CLOCK_PIT_INPUT_HZ + kDenominator - 1) /
                    kDenominator <= GTOS_EVENT_TIMEOUT_LIMIT);
  DCHECK(remaining.is_positive());
  const auto micros = static_cast<uint64_t>(std::min(remaining.InMicroseconds(), kMaxIntervalUs));
  return static_cast<unsigned>((micros * GTOS_CLOCK_PIT_INPUT_HZ + kDenominator - 1) /
                               kDenominator);
}

size_t WaitForever(span<const unsigned> handles) {
  for (;;) {
    if (auto ready = WaitOnce(handles, GTOS_EVENT_TIMEOUT_LIMIT)) {
      return *ready;
    }
  }
}

}  // namespace

void WaitableEvent::NativeEventTraits::Free(unsigned handle) {
  ControlEvent(GTOS_SYS_EVENT_CLOSE, handle);
}

WaitableEvent::WaitableEvent(ResetPolicy reset, InitialState initial)
    : handle_(CreateEvent(reset, initial)),
      kernel_(MakeRefCounted<WaitableEventKernel>(reset, initial)) {}

void WaitableEvent::Reset() {
  AutoLock locked(kernel_->lock_);
  ControlEvent(GTOS_SYS_EVENT_RESET, handle_.get());
}

void WaitableEvent::SignalImpl() {
  // A waiter can destroy this event after notification. Keep the queue alive
  // and avoid accessing event members after waking anyone.
  const scoped_refptr<WaitableEventKernel> kernel = kernel_;
  const unsigned handle = handle_.get();
  AutoLock locked(kernel->lock_);
  if (kernel->manual_reset_) {
    ControlEvent(GTOS_SYS_EVENT_SET, handle);
    kernel->SignalAll(this);
  } else if (!kernel->SignalOne(this)) {
    ControlEvent(GTOS_SYS_EVENT_SET, handle);
  }
}

bool WaitableEvent::IsSignaled() const {
  const unsigned handle = handle_.get();
  return WaitOnce(span_from_ref(handle), 0).has_value();
}

bool WaitableEvent::IsDefinitelySignaled() const {
  return IsSignaled();
}

bool WaitableEvent::TimedWaitImpl(TimeDelta wait_delta) {
  const unsigned handle = handle_.get();
  if (wait_delta.is_max()) {
    WaitForever(span_from_ref(handle));
    return true;
  }
  const auto begin = subtle::TimeTicksNowIgnoringOverride();
  TimeDelta remaining = wait_delta;
  do {
    if (WaitOnce(span_from_ref(handle), TimeoutTicks(remaining))) {
      return true;
    }
    // The first scheduler tick may already be partially elapsed.
    remaining = wait_delta - (subtle::TimeTicksNowIgnoringOverride() - begin);
  } while (remaining.is_positive());
  return false;
}

size_t WaitableEvent::WaitManyImpl(span<WaitableEvent*> waitables) {
  std::array<unsigned, GTOS_EVENT_WAIT_LIMIT> handles;
  auto values = span(handles).first(waitables.size());
  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = waitables[i]->handle_.get();
  }
  return WaitForever(values);
}

}  // namespace base

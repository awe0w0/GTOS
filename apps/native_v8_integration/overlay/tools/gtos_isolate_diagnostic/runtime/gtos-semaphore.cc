// Copyright 2026 the GTOS project authors.
#include "src/base/platform/semaphore.h"
#include "src/base/platform/time.h"
#include "src/base/platform/gtos-clock-bridge.h"
#include "src/base/platform/gtos-native-services.h"
#include "thread-context.h"

namespace v8 {
namespace base {
namespace {
bool Consume(uint32_t* count) {
  uint32_t available = __atomic_load_n(count, __ATOMIC_ACQUIRE);
  for (;;) {
    if (available > 0x7fffffffU) gtos::UnsupportedService();
    if (available == 0) return false;
    if (__atomic_compare_exchange_n(count, &available, available - 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return true;
  }
}
}  // namespace
Semaphore::Semaphore(int count) {
  gtos::CurrentNativeThreadInfo();
  if (count < 0) gtos::UnsupportedService();
  native_handle_ = static_cast<uint32_t>(count);
}
Semaphore::~Semaphore() {
  if (__atomic_load_n(&native_handle_, __ATOMIC_RELAXED) > 0x7fffffffU)
    gtos::UnsupportedService();
}
void Semaphore::Signal() {
  gtos::CurrentNativeThreadInfo();
  uint32_t count = __atomic_load_n(&native_handle_, __ATOMIC_RELAXED);
  for (;;) {
    if (count >= 0x7fffffffU) gtos::UnsupportedService();
    if (__atomic_compare_exchange_n(&native_handle_, &count, count + 1, false,
                                    __ATOMIC_RELEASE, __ATOMIC_RELAXED)) return;
  }
}
void Semaphore::Wait() {
  gtos::CurrentNativeThreadInfo();
  while (!Consume(&native_handle_)) gtos::Yield();
}
bool Semaphore::WaitFor(const TimeDelta& relative) {
  gtos::CurrentNativeThreadInfo();
  if (Consume(&native_handle_)) return true;
  const int64_t duration = relative.InMicroseconds();
  if (duration <= 0) return false;
  const int64_t begin = gtos_v8_clock_read_microseconds();
  for (;;) {
    gtos::Yield();
    if (Consume(&native_handle_)) return true;
    const int64_t now = gtos_v8_clock_read_microseconds();
    if (now < begin) gtos::UnsupportedService();
    if (now - begin >= duration) return false;
  }
}
}  // namespace base
}  // namespace v8

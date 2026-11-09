// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_record.h"

#include <errno.h>
#include <process/system_memory_abi.h>

#include "src/base/once.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/base/platform/time.h"
#include "src/base/sys-info.h"
#include "tools/gtos_native_sdk/native-thread.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

bool Check(bool value) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!value)
    native_memory_record.failures = native_memory_record.failures + 1;
  return value;
}

struct State {
  v8::base::OnceType once{v8::base::ONCE_STATE_UNINITIALIZED};
  unsigned started = 0;
  unsigned entered = 0;
  unsigned release = 0;
  unsigned returned = 0;
  unsigned calls = 0;
  unsigned value = 0;
};

bool Wait(unsigned* value) {
  for (unsigned i = 0; i < 4096; ++i) {
    if (__atomic_load_n(value, __ATOMIC_ACQUIRE))
      return true;
    v8::base::gtos::Yield();
  }
  return false;
}

void* Initialize(void* argument) {
  auto& state = *static_cast<State*>(argument);
  v8::base::CallOnceImpl(&state.once, [&] {
    ++state.calls;
    __atomic_store_n(&state.started, 1U, __ATOMIC_RELEASE);
    if (Wait(&state.release))
      state.value = 42;
  });
  return argument;
}

void* Observe(void* argument) {
  auto& state = *static_cast<State*>(argument);
  __atomic_store_n(&state.entered, 1U, __ATOMIC_RELEASE);
  v8::base::CallOnceImpl(&state.once, [&] {
    ++state.calls;
    state.value = 99;
  });
  __atomic_store_n(&state.returned, state.value, __ATOMIC_RELEASE);
  return argument;
}

}  // namespace

extern "C" int NativeEntry() {
  using v8::base::SysInfo;
  using v8::base::Time;
  using v8::base::TimeTicks;
  const auto process = v8::base::gtos::CurrentProcessInfo();
  native_memory_record.base = process.process_id;
  GtosSystemMemoryInfo memory{};
  const GtosSystemMemoryRequest request{GTOS_SYSTEM_MEMORY_ABI_VERSION,
                                       reinterpret_cast<unsigned>(&memory)};
  unsigned operation = GTOS_SYS_SYSTEM_MEMORY_INFO;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  Check(operation == 0 && memory.version == GTOS_SYSTEM_MEMORY_ABI_VERSION);
  Check(SysInfo::NumberOfProcessors() == static_cast<int>(process.scheduler_cpu_count));
  Check(SysInfo::AmountOfVirtualMemory() ==
        static_cast<int64_t>(process.maximum_pages) * process.page_bytes);
  Check(SysInfo::AddressSpaceEnd() == process.user_end);
  Check(memory.total_frames > 0 && memory.free_frames <= memory.total_frames &&
        SysInfo::AmountOfPhysicalMemory() ==
            static_cast<int64_t>(memory.total_frames) * memory.page_bytes);
  const auto before = TimeTicks::Now();
  const auto after = TimeTicks::Now();
  Check(!before.IsNull());
  Check(after >= before);
  Check(!TimeTicks::IsHighResolution());
  Check(!v8::base::ThreadTicks::IsSupported());
  constexpr int64_t epoch = 946684800LL * 1000000;
  const auto calendar = Time::Now();
  const auto calendar_after = Time::NowFromSystemTime();
  Check(calendar.ToInternalValue() >= epoch &&
        calendar.ToInternalValue() < epoch + 86400LL * 1000000);
  Check(calendar_after >= calendar &&
        calendar_after.ToInternalValue() < epoch + 86400LL * 1000000);

  v8::base::OnceType fast{v8::base::ONCE_STATE_UNINITIALIZED};
  unsigned calls = 0;
  v8::base::CallOnceImpl(&fast, [&] { ++calls; });
  v8::base::CallOnceImpl(&fast, [&] { ++calls; });
  Check(calls == 1 && fast.load() == v8::base::ONCE_STATE_DONE);

  State state;
  unsigned first = 0;
  unsigned second = 0;
  if (!Check(gtos::native::StartThread(16384, Initialize, &state, &first) == 0))
    return 2;
  Check(Wait(&state.started));
  if (!Check(gtos::native::StartThread(16384, Observe, &state, &second) == 0)) {
    __atomic_store_n(&state.release, 1U, __ATOMIC_RELEASE);
    gtos::native::JoinThread(first);
    return 3;
  }
  Check(Wait(&state.entered));
  for (unsigned i = 0; i < 16; ++i)
    v8::base::gtos::Yield();
  Check(__atomic_load_n(&state.returned, __ATOMIC_ACQUIRE) == 0);
  __atomic_store_n(&state.release, 1U, __ATOMIC_RELEASE);
  void* result = nullptr;
  Check(gtos::native::JoinThread(first, &result) == 0 && result == &state);
  result = nullptr;
  Check(gtos::native::JoinThread(second, &result) == 0 && result == &state);
  Check(state.calls == 1 && state.value == 42 && state.returned == 42);
  Check(state.once.load() == v8::base::ONCE_STATE_DONE);
  v8::base::CallOnceImpl(&state.once, [&] { state.value = 99; });
  Check(state.value == 42);
  Check(first > process.thread_id && second > first);
  native_memory_record.fault_address = second;
  Check(gtos::native::JoinThread(second) == ESRCH);
#if defined(GTOS_V8_PLATFORM_GUEST_WRONG_EXPECTATION)
  Check(SysInfo::NumberOfProcessors() == 0);
#else
  Check(SysInfo::NumberOfProcessors() > 0);
#endif
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

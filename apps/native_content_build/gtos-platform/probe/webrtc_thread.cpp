#include "memory_record.h"

#include <__external_threading>
#include <cstdint>
#include <utility>

#include "rtc_base/deprecated/recursive_critical_section.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/time_utils.h"
#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

void Check(bool passed) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!passed) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
}

bool CollectOne() {
  for (unsigned attempt = 0; attempt < 4096; ++attempt) {
    if (gtos::native::CollectDetachedThreads() == 1) {
      return true;
    }
    std::__libcpp_thread_yield();
  }
  return false;
}

}  // namespace

extern "C" int NativeEntry() {
  const auto info = gtos::native::CurrentProcessInfo();
  const auto main = webrtc::CurrentThreadId();
  const auto main_ref = webrtc::CurrentThreadRef();
  native_memory_record.base = info.process_id;
  Check(main == info.process_id);
  Check(webrtc::IsThreadRefEqual(main, main_ref));

  webrtc::RecursiveCriticalSection critical_section;
  critical_section.Enter();
  const bool recursive = critical_section.TryEnter();
  Check(recursive);
  if (recursive) {
    critical_section.Leave();
  }
  unsigned worker_id = 0;
  unsigned lock_attempt = 0;
  bool released = false;
  bool worker_acquired = false;
  bool worker_has_own_ref = false;
  auto joinable = webrtc::PlatformThread::SpawnJoinable([&] {
    const auto id = webrtc::CurrentThreadId();
    const auto ref = webrtc::CurrentThreadRef();
    worker_has_own_ref = webrtc::IsThreadRefEqual(id, ref) &&
                         !webrtc::IsThreadRefEqual(main_ref, ref);
    const bool contended = !critical_section.TryEnter();
    if (!contended) {
      critical_section.Leave();
    }
    __atomic_store_n(&lock_attempt, contended ? 1 : 2, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&released, __ATOMIC_ACQUIRE)) {
      std::__libcpp_thread_yield();
    }
    worker_acquired = critical_section.TryEnter();
    if (worker_acquired) {
      critical_section.Leave();
    }
    __atomic_store_n(&worker_id, id, __ATOMIC_RELEASE);
  }, "gtos-join");
  const auto handle = joinable.GetHandle();
  Check(handle.has_value() && *handle > main);
  webrtc::PlatformThread moved(std::move(joinable));
  Check(joinable.empty() && moved.GetHandle() == handle);
  unsigned attempt;
  while ((attempt = __atomic_load_n(&lock_attempt, __ATOMIC_ACQUIRE)) == 0) {
    std::__libcpp_thread_yield();
  }
  Check(attempt == 1);
  critical_section.Leave();
  __atomic_store_n(&released, true, __ATOMIC_RELEASE);
  moved.Finalize();
  Check(moved.empty());
  native_memory_record.fault_address = __atomic_load_n(&worker_id, __ATOMIC_ACQUIRE);
  Check(native_memory_record.fault_address == handle.value_or(0));
  Check(worker_has_own_ref);
  Check(worker_acquired);
  const bool reacquired = critical_section.TryEnter();
  Check(reacquired);
  if (reacquired) {
    critical_section.Leave();
  }

  bool release = false;
  unsigned detached_id = 0;
  auto detached = webrtc::PlatformThread::SpawnDetached([&] {
    while (!__atomic_load_n(&release, __ATOMIC_ACQUIRE)) {
      std::__libcpp_thread_yield();
    }
    __atomic_store_n(&detached_id, webrtc::CurrentThreadId(), __ATOMIC_RELEASE);
  }, "gtos-detach");
  const auto detached_handle = detached.GetHandle();
  Check(detached_handle.has_value() && detached_handle.value() > handle.value_or(0));
  detached.Finalize();
  Check(detached.empty());
  __atomic_store_n(&release, true, __ATOMIC_RELEASE);
  Check(CollectOne());
  Check(__atomic_load_n(&detached_id, __ATOMIC_ACQUIRE) == detached_handle.value_or(0));

  constexpr int64_t kEpochMicros = 946684800LL * 1000000;
  const int64_t before = webrtc::TimeUTCMicros();
  const int64_t after = webrtc::TimeUTCMicros();
  Check(before >= kEpochMicros && before < kEpochMicros + 86400LL * 1000000);
  Check(after >= before && after < kEpochMicros + 86400LL * 1000000);
#if defined(GTOS_WEBRTC_THREAD_GUEST_WRONG_EXPECTATION)
  Check(gtos::native::CollectDetachedThreads() == 1);
#else
  Check(gtos::native::CollectDetachedThreads() == 0);
#endif
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

#include "memory_record.h"

#include <__external_threading>
#include <errno.h>
#include <process/thread_abi.h>

#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
int __cxa_thread_atexit(void (*destroy)(void*), void* object, void* dso) noexcept;
}

namespace {

using gtos::native::CollectDetachedThreads;
using gtos::native::CurrentProcessInfo;
using gtos::native::DetachThread;
using gtos::native::JoinThread;
using gtos::native::StartThread;

constexpr unsigned kStackBytes = 8192;
constexpr unsigned kAttempts = 4096;

struct Job {
  bool release = false;
  bool self_detach = false;
  unsigned owner = 0;
  unsigned cleaned = 0;
  int self_join = 0;
  int detach = -1;
};

bool Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
  return ok;
}

void Destroy(void* object) {
  auto& job = *static_cast<Job*>(object);
  __atomic_store_n(&job.cleaned, CurrentProcessInfo().thread_id, __ATOMIC_RELEASE);
}

void* Worker(void* argument) {
  auto& job = *static_cast<Job*>(argument);
  const unsigned id = CurrentProcessInfo().thread_id;
  job.self_join = JoinThread(id);
  if (__cxa_thread_atexit(Destroy, &job, nullptr) != 0) {
    return nullptr;
  }
  __atomic_store_n(&job.owner, id, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&job.release, __ATOMIC_ACQUIRE)) {
    std::__libcpp_thread_yield();
  }
  if (job.self_detach) {
    job.detach = DetachThread(id);
  }
  return &job;
}

unsigned WaitOwner(const Job& job) {
  for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
    const unsigned owner = __atomic_load_n(&job.owner, __ATOMIC_ACQUIRE);
    if (owner != 0) {
      return owner;
    }
    std::__libcpp_thread_yield();
  }
  return 0;
}

bool WaitCollection() {
  for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
    if (CollectDetachedThreads() == 1) {
      return true;
    }
    std::__libcpp_thread_yield();
  }
  return false;
}

void Release(Job& job) {
  __atomic_store_n(&job.release, true, __ATOMIC_RELEASE);
}

int Finish() {
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

}  // namespace

extern "C" int NativeEntry() {
  const auto process = CurrentProcessInfo();
  native_memory_record.base = process.process_id;
  Check(process.thread_id == process.process_id && process.maximum_parallel_threads == 4);
  unsigned untouched = 0xabcdefU;
  Check(StartThread(kStackBytes, nullptr, nullptr, &untouched) == EINVAL && untouched == 0xabcdefU);
  Check(StartThread(1024 * 1024, Worker, nullptr, &untouched) == EINVAL && untouched == 0xabcdefU);
  Check(CollectDetachedThreads() == 0);

  Job jobs[3];
  jobs[2].self_detach = true;
  unsigned ids[3]{};
  for (unsigned i = 0; i < 3; ++i) {
    if (!Check(StartThread(kStackBytes, Worker, &jobs[i], &ids[i]) == 0 &&
               ids[i] > process.process_id)) {
      return Finish();
    }
  }
  native_memory_record.fault_address = ids[0];
  Check(ids[0] != ids[1] && ids[1] != ids[2] && ids[0] != ids[2]);
  Check(StartThread(kStackBytes, Worker, nullptr, &untouched) == -GTOS_THREAD_ERR_LIMIT &&
        untouched == 0xabcdefU);
  for (unsigned i = 0; i < 3; ++i) {
    Check(WaitOwner(jobs[i]) == ids[i] && jobs[i].self_join == -GTOS_THREAD_ERR_DEADLOCK);
  }

  Check(DetachThread(ids[1]) == 0);
  Check(DetachThread(ids[1]) == EINVAL);
  Check(JoinThread(ids[1]) == EINVAL);
  Check(CollectDetachedThreads() == 0);
  Release(jobs[0]);
  Release(jobs[1]);
  void* result = nullptr;
  Check(JoinThread(ids[0], &result) == 0 && result == &jobs[0] &&
        __atomic_load_n(&jobs[0].cleaned, __ATOMIC_ACQUIRE) == ids[0]);
  Check(JoinThread(ids[0]) == -GTOS_THREAD_ERR_NO_THREAD);
  Check(WaitCollection() && __atomic_load_n(&jobs[1].cleaned, __ATOMIC_ACQUIRE) == ids[1]);
  Check(JoinThread(ids[1]) == -GTOS_THREAD_ERR_NO_THREAD);
  Release(jobs[2]);
  Check(WaitCollection() && __atomic_load_n(&jobs[2].cleaned, __ATOMIC_ACQUIRE) == ids[2] &&
        jobs[2].detach == 0);
  Check(JoinThread(ids[2]) == -GTOS_THREAD_ERR_NO_THREAD);

  Job reused;
  Release(reused);
  std::__libcpp_thread_t standard = 0;
  Check(std::__libcpp_thread_create(&standard, Worker, &reused) == 0 && standard > ids[2]);
  Check(std::__libcpp_thread_join(&standard) == 0 &&
        __atomic_load_n(&reused.cleaned, __ATOMIC_ACQUIRE) == standard);
#if defined(GTOS_NATIVE_THREAD_GUEST_WRONG_EXPECTATION)
  Check(CollectDetachedThreads() == 1);
#else
  Check(CollectDetachedThreads() == 0);
#endif
  return Finish();
}

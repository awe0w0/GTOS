#include "memory_record.h"

#include <__external_threading>
#include <atomic>
#include <initializer_list>

#include "third_party/skia/include/private/SkSemaphore.h"
#include "third_party/skia/include/private/SkThreadID.h"
#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

struct Shared {
  SkSemaphore gate;
  std::atomic<unsigned> ready{0};
  std::atomic<unsigned> finished{0};
  std::atomic<unsigned> received{0};
  std::atomic<unsigned> value{0};
};

struct Worker {
  Shared& shared;
  unsigned id = 0;
};

bool Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
  return ok;
}

int Finish() {
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

bool WaitCount(const std::atomic<unsigned>& count, unsigned expected) {
  for (unsigned attempt = 0; attempt < 4096; ++attempt) {
    if (count.load(std::memory_order_acquire) == expected) {
      return true;
    }
    std::__libcpp_thread_yield();
  }
  return false;
}

void* Wait(void* argument) {
  auto& worker = *static_cast<Worker*>(argument);
  auto& shared = worker.shared;
  worker.id = SkGetThreadID();
  shared.ready.fetch_add(1, std::memory_order_release);
  shared.gate.wait();
  if (shared.value.load(std::memory_order_relaxed) == 0x5741) {
    shared.received.fetch_add(1, std::memory_order_relaxed);
  }
  shared.finished.fetch_add(1, std::memory_order_release);
  return argument;
}

}  // namespace

extern "C" int NativeEntry() {
  native_memory_record.base = gtos::native::CurrentProcessInfo().process_id;
  Check(SkGetThreadID() == native_memory_record.base);
  SkSemaphore immediate;
  Check(!immediate.try_wait());
  immediate.signal(2);
  Check(immediate.try_wait() && immediate.try_wait() && !immediate.try_wait());

  for (unsigned batch : {1u, 2u}) {
    Shared shared;
    Worker workers[]{{shared}, {shared}};
    unsigned ids[2]{};
    for (unsigned i = 0; i < 2; ++i) {
      if (!Check(gtos::native::StartThread(8192, Wait, &workers[i], &ids[i]) == 0)) {
        if (i != 0) {
          shared.gate.signal();
          gtos::native::JoinThread(ids[0]);
        }
        return Finish();
      }
    }
    native_memory_record.fault_address = ids[0];
    Check(WaitCount(shared.ready, 2));
    std::__libcpp_thread_yield();
    std::__libcpp_thread_yield();
    Check(shared.finished.load(std::memory_order_acquire) == 0);
    shared.value.store(0x5741, std::memory_order_relaxed);
    shared.gate.signal(batch);
    if (batch == 1) {
      Check(WaitCount(shared.finished, 1));
      shared.gate.signal();
    }

    void* result = nullptr;
    for (unsigned i = 0; i < 2; ++i) {
      Check(gtos::native::JoinThread(ids[i], &result) == 0 &&
            result == &workers[i] && workers[i].id == ids[i]);
    }
    Check(shared.finished.load(std::memory_order_acquire) == 2 &&
          shared.received.load(std::memory_order_relaxed) == 2);
  }

#if defined(GTOS_SKIA_SEMAPHORE_GUEST_WRONG_EXPECTATION)
  Check(immediate.try_wait());
#else
  Check(!immediate.try_wait());
#endif
  return Finish();
}

#include "memory_record.h"

#include <process/abi.h>
#include <process/thread_abi.h>
#include <process/thread_slot_abi.h>
#include <process/vm_abi.h>

#include "base/threading/thread_local_storage.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
void gtos_native_thread_cleanup() noexcept;
[[noreturn]] void gtos_native_exit_with_tls(unsigned) noexcept;
}

namespace {

using Tls = base::internal::PlatformThreadLocalStorage;
Tls::TLSKey key;
int root_value, worker_value;
unsigned root_destructors, worker_destructors;

template<class Request>
int Call(unsigned operation, const Request& request) {
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}

bool Check(bool ok) {
  __atomic_fetch_add(&native_memory_record.checks, 1, __ATOMIC_RELAXED);
  if (!ok) {
    __atomic_fetch_add(&native_memory_record.failures, 1, __ATOMIC_RELAXED);
  }
  return ok;
}

GtosThreadSlotInfo CurrentThreadSlot() {
  GtosThreadSlotInfo info{};
  const GtosThreadSlotRequest request{GTOS_THREAD_SLOT_ABI_VERSION,
                                     reinterpret_cast<unsigned>(&info)};
  Check(Call(GTOS_SYS_THREAD_SLOT_INFO, request) == 0 &&
        info.version == GTOS_THREAD_SLOT_ABI_VERSION &&
        info.index < GTOS_THREAD_SLOT_LIMIT && info.thread_id != 0);
  return info;
}

struct WorkerResult {
  GtosThreadSlotInfo slot;
};

[[noreturn]] void Worker(unsigned argument) {
  auto& result = *reinterpret_cast<WorkerResult*>(argument);
  result.slot = CurrentThreadSlot();
  Check(Tls::GetTLSValue(key) == nullptr);
  Tls::SetTLSValue(key, &worker_value);
  Tls::SetTLSValue(key, &worker_value);
  Check(Tls::GetTLSValue(key) == &worker_value);
  gtos_native_exit_with_tls(0);
}

bool RunWorker(unsigned stack, WorkerResult& result) {
  unsigned thread = 0;
  const GtosThreadCreateRequest create{GTOS_THREAD_ABI_VERSION, 0,
      reinterpret_cast<unsigned>(&Worker), reinterpret_cast<unsigned>(&result),
      stack, reinterpret_cast<unsigned>(&thread)};
  if (!Check(Call(GTOS_SYS_THREAD_CREATE, create) == 0 && thread != 0)) {
    return false;
  }
  unsigned exit = 1;
  const GtosThreadJoinRequest join{GTOS_THREAD_ABI_VERSION, thread,
                                  reinterpret_cast<unsigned>(&exit)};
  int rc;
  do {
    rc = Call(GTOS_SYS_THREAD_TRY_JOIN, join);
    if (rc == GTOS_THREAD_ERR_BUSY) {
      unsigned operation = GTOS_SYS_YIELD;
      asm volatile("int $0x80" : "+a"(operation) : "b"(0U), "c"(0U) : "memory", "cc");
    }
  } while (rc == GTOS_THREAD_ERR_BUSY);
  return Check(rc == 0 && exit == 0 && result.slot.thread_id == thread);
}

}  // namespace

// Test consumer of the real platform callback and native __cxa_thread_atexit.
// Generic Chromium slot-destruction policy is compiled in the formal graph.
void Tls::OnThreadExit() {
  void* value = GetTLSValue(key);
  Check(value == &root_value || value == &worker_value);
  if (value == &root_value) {
    ++root_destructors;
  } else {
    ++worker_destructors;
  }
}

extern "C" int NativeEntry() {
  const auto root = CurrentThreadSlot();
  native_memory_record.base = root.thread_id;
  GtosThreadSlotInfo sentinel{17, 0, 0};
  Check(Call(GTOS_SYS_THREAD_SLOT_INFO,
             GtosThreadSlotRequest{0, reinterpret_cast<unsigned>(&sentinel)}) ==
            GTOS_THREAD_SLOT_ERR_VERSION &&
        sentinel.version == 17);
  if (!Check(Tls::AllocTLS(&key) && key != Tls::TLS_KEY_OUT_OF_INDEXES)) {
    return 2;
  }
  Check(Tls::GetTLSValue(key) == nullptr);
  Tls::SetTLSValue(key, &root_value);
  Tls::SetTLSValue(key, &root_value);

  GtosVmReserveResult stack{};
  const GtosVmReserveRequest reserve{GTOS_VM_ABI_VERSION, 4 * GTOS_VM_PAGE_BYTES,
      GTOS_VM_PAGE_BYTES, 0, reinterpret_cast<unsigned>(&stack)};
  if (!Check(Call(GTOS_SYS_VM_RESERVE, reserve) == 0 && stack.handle != 0) ||
      !Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, GtosVmRangeRequest{GTOS_VM_ABI_VERSION,
          stack.handle, GTOS_VM_PAGE_BYTES, 2 * GTOS_VM_PAGE_BYTES, GTOS_VM_READ_WRITE}) == 0)) {
    return 2;
  }
  WorkerResult first{}, second{};
  if (!RunWorker(stack.handle, first) || !RunWorker(stack.handle, second)) {
    return 2;
  }
  Check(first.slot.index == second.slot.index && root.index != first.slot.index);
  Check(first.slot.thread_id != second.slot.thread_id);
  Check(worker_destructors == 2 && root_destructors == 0);
  Check(Tls::GetTLSValue(key) == &root_value);
  Check(Call(GTOS_SYS_VM_RELEASE, GtosVmControlRequest{GTOS_VM_ABI_VERSION,
                                                     stack.handle, 0}) == 0);
  gtos_native_thread_cleanup();
  Check(root_destructors == 1 && worker_destructors == 2);

  const auto old_key = key;
  Tls::FreeTLS(key);
  Check(Tls::AllocTLS(&key) && key != old_key);
  Check(Tls::GetTLSValue(old_key) == nullptr && Tls::GetTLSValue(key) == nullptr);
#if defined(GTOS_BASE_TLS_GUEST_WRONG_EXPECTATION)
  Check(Tls::GetTLSValue(key) == &root_value);
#else
  Check(Tls::GetTLSValue(key) == nullptr);
#endif
  Tls::FreeTLS(key);
  native_memory_record.fault_address = second.slot.thread_id;
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

#include "memory_record.h"

#include <process/thread_abi.h>
#include <process/vm_abi.h>

#include "base/process/process_handle.h"
#include "base/synchronization/lock.h"
#include "base/threading/platform_thread.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

base::PlatformThreadRef main_ref;
unsigned main_id;

struct WorkerContext {
  base::Lock lock;
  unsigned process;
  unsigned tried = 0;
  unsigned ready = 0;
  int protected_value = 0;
};

template <class Request>
int Call(unsigned operation, const Request& request) {
  asm volatile("int $0x80" : "+a"(operation) : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}

bool Check(bool ok) {
  __atomic_fetch_add(&native_memory_record.checks, 1, __ATOMIC_RELAXED);
  if (!ok)
    __atomic_fetch_add(&native_memory_record.failures, 1, __ATOMIC_RELAXED);
  return ok;
}

[[noreturn]] void Worker(unsigned argument) {
  auto& context = *reinterpret_cast<WorkerContext*>(argument);
  const unsigned process = context.process;
  const unsigned thread = base::PlatformThread::CurrentId().raw();
  const auto ref = base::PlatformThread::CurrentRef();
  native_memory_record.fault_address = thread;
  Check(thread != 0 && thread != main_id);
  Check(!ref.is_null() && ref != main_ref);
  Check(base::PlatformThread::CurrentHandle().platform_handle() == thread);
  Check(static_cast<unsigned>(base::GetCurrentProcId()) == process);
  Check(base::GetUniqueIdForProcess().GetUnsafeValue() == static_cast<int32_t>(process));
  const bool acquired_early = context.lock.Try();
  if (!Check(!acquired_early))
    context.lock.Release();
  __atomic_store_n(&context.tried, 1, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&context.ready, __ATOMIC_ACQUIRE))
    base::PlatformThread::YieldCurrentThread();
  if (Check(context.lock.Try())) {
    Check(context.protected_value == 42);
    context.protected_value = 43;
    context.lock.Release();
  }
  unsigned operation = GTOS_SYS_THREAD_EXIT;
  asm volatile("int $0x80" : "+a"(operation) : "b"(0U) : "memory", "cc");
  __builtin_trap();
}

}  // namespace

extern "C" int NativeEntry() {
  const auto process = base::GetCurrentProcId();
  const auto handle = base::GetCurrentProcessHandle();
  main_id = base::PlatformThread::CurrentId().raw();
  main_ref = base::PlatformThread::CurrentRef();
  native_memory_record.base = process;
  Check(process > 0 && handle == process && base::GetProcId(handle) == process);
  Check(base::GetUniqueIdForProcess().GetUnsafeValue() == process);
  Check(base::GetParentProcessId(handle) < 0);
  Check(main_id == static_cast<unsigned>(process) && !main_ref.is_null());
  Check(base::PlatformThread::CurrentHandle().platform_handle() == main_id);
  WorkerContext context;
  context.process = static_cast<unsigned>(process);
  if (!Check(context.lock.Try()))
    return 2;

  GtosVmReserveResult region{};
  const GtosVmReserveRequest reserve{GTOS_VM_ABI_VERSION, 4 * GTOS_VM_PAGE_BYTES,
      GTOS_VM_PAGE_BYTES, 0, reinterpret_cast<unsigned>(&region)};
  if (!Check(Call(GTOS_SYS_VM_RESERVE, reserve) == 0 && region.handle != 0)) {
    context.lock.Release();
    return 2;
  }
  const GtosVmRangeRequest writable{GTOS_VM_ABI_VERSION, region.handle,
      GTOS_VM_PAGE_BYTES, 2 * GTOS_VM_PAGE_BYTES, GTOS_VM_READ_WRITE};
  if (!Check(Call(GTOS_SYS_VM_SET_PERMISSIONS, writable) == 0)) {
    context.lock.Release();
    return 3;
  }
  unsigned worker = 0;
  const GtosThreadCreateRequest create{GTOS_THREAD_ABI_VERSION, 0,
      reinterpret_cast<unsigned>(&Worker), reinterpret_cast<unsigned>(&context),
      region.handle, reinterpret_cast<unsigned>(&worker)};
  if (!Check(Call(GTOS_SYS_THREAD_CREATE, create) == 0 && worker != 0)) {
    context.lock.Release();
    return 4;
  }
  while (!__atomic_load_n(&context.tried, __ATOMIC_ACQUIRE))
    base::PlatformThread::YieldCurrentThread();
  context.protected_value = 42;
  context.lock.Release();
  __atomic_store_n(&context.ready, 1, __ATOMIC_RELEASE);
  unsigned exit = 1;
  const GtosThreadJoinRequest join{GTOS_THREAD_ABI_VERSION, worker,
      reinterpret_cast<unsigned>(&exit)};
  int result;
  do {
    result = Call(GTOS_SYS_THREAD_TRY_JOIN, join);
    if (result == GTOS_THREAD_ERR_BUSY)
      base::PlatformThread::YieldCurrentThread();
  } while (result == GTOS_THREAD_ERR_BUSY);
  Check(result == 0 && exit == 0);
  if (Check(context.lock.Try())) {
    Check(context.protected_value == 43);
    context.lock.Release();
  }
  const GtosVmControlRequest release{GTOS_VM_ABI_VERSION, region.handle, 0};
  Check(Call(GTOS_SYS_VM_RELEASE, release) == 0);
  Check(base::PlatformThread::CurrentId().raw() == main_id);
#if defined(GTOS_BASE_IDENTITY_GUEST_WRONG_EXPECTATION)
  Check(native_memory_record.fault_address == main_id);
#else
  Check(native_memory_record.fault_address == worker);
#endif
  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

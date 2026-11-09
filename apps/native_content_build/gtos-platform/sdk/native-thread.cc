// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gtos_native_sdk/native-thread.h"

#include <__external_threading>
#include <errno.h>
#include <process/thread_abi.h>
#include <process/thread_slot_abi.h>
#include <process/vm_abi.h>

#include <array>
#include <cstddef>
#include <cstdlib>

#include "tools/gtos_native_sdk/native-services.h"

extern "C" [[noreturn]] void gtos_native_exit_with_tls(unsigned) noexcept;
extern "C" void* __emutls_get_address(void*);
extern "C" int __cxa_thread_atexit(void (*)(void*), void*, void*) noexcept;
extern "C" void* __dso_handle;

namespace {

struct TlsValue {
  void* pointer;
  void (*destroy)(void*);
};

struct TlsKey {
  uintptr_t size;
  uintptr_t alignment;
  uintptr_t index;
  const void* initial;
  void (*destroy)(void*);
};
static_assert(sizeof(uintptr_t) == 4 && offsetof(TlsKey, destroy) == 16,
              "GTOS uses the IA32 compiler emutls control prefix");

TlsValue& TlsCell(TlsKey* key) {
  return *static_cast<TlsValue*>(__emutls_get_address(key));
}

void DestroyTlsCell(void* object) {
  auto& cell = *static_cast<TlsValue*>(object);
  const auto value = cell;
  cell = {};
  if (value.pointer) {
    value.destroy(value.pointer);
  }
}

}  // namespace

extern "C" int gtos_libcpp_tls_create(uint32_t* key, void (*destroy)(void*)) {
  if (!key) {
    return EINVAL;
  }
  // libc++ retains its TLS key for the life of the process.
  auto* control = static_cast<TlsKey*>(std::malloc(sizeof(TlsKey)));
  if (!control) {
    return ENOMEM;
  }
  *control = {sizeof(TlsValue), alignof(TlsValue), 0, nullptr, destroy};
  *key = reinterpret_cast<uintptr_t>(control);
  return 0;
}

extern "C" void* gtos_libcpp_tls_get(uint32_t key) {
  return key ? TlsCell(reinterpret_cast<TlsKey*>(key)).pointer : nullptr;
}

extern "C" int gtos_libcpp_tls_set(uint32_t key, void* value) {
  if (!key) {
    return EINVAL;
  }
  auto* control = reinterpret_cast<TlsKey*>(key);
  auto& cell = TlsCell(control);
  if (value && control->destroy && !cell.destroy) {
    if (__cxa_thread_atexit(DestroyTlsCell, &cell, __dso_handle) != 0) {
      return ENOMEM;
    }
    cell.destroy = control->destroy;
  }
  cell.pointer = value;
  return 0;
}

namespace gtos::native {
namespace {

constexpr unsigned kGuardBytes = GTOS_VM_PAGE_BYTES;
constexpr unsigned kMaximumStackBytes = 62 * GTOS_VM_PAGE_BYTES;

enum class State { kEmpty, kJoinable, kJoining, kDetached };
struct Worker {
  State state = State::kEmpty;
  uint32_t id = 0;
  unsigned stack = 0;
  ThreadRoutine routine = nullptr;
  void* argument = nullptr;
  bool ready = false;
};

std::array<Worker, GTOS_THREAD_SLOT_LIMIT - 1> workers;
std::__libcpp_mutex_t mutex = _LIBCPP_MUTEX_INITIALIZER;

class WorkerLock {
 public:
  WorkerLock() {
    if (std::__libcpp_mutex_lock(&mutex) != 0) {
      UnsupportedService();
    }
  }
  ~WorkerLock() {
    if (std::__libcpp_mutex_unlock(&mutex) != 0) {
      UnsupportedService();
    }
  }
  WorkerLock(const WorkerLock&) = delete;
  WorkerLock& operator=(const WorkerLock&) = delete;
};

template <class Request>
int Call(unsigned operation, const Request& request) {
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}

void Yield() {
  unsigned operation = GTOS_SYS_YIELD;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(0U), "c"(0U) : "memory", "cc");
  if (operation != 0) {
    UnsupportedService();
  }
}

[[noreturn]] void RunWorker(unsigned argument) {
  const auto& worker = *reinterpret_cast<Worker*>(argument);
  if (!__atomic_load_n(&worker.ready, __ATOMIC_ACQUIRE)) {
    UnsupportedService();
  }
  void* result = worker.routine(worker.argument);
  gtos_native_exit_with_tls(reinterpret_cast<uintptr_t>(result));
}

Worker* Find(uint32_t id) {
  for (auto& worker : workers) {
    if (worker.state != State::kEmpty && worker.id == id) {
      return &worker;
    }
  }
  return nullptr;
}

void ReleaseStack(unsigned handle) {
  const GtosVmControlRequest release{GTOS_VM_ABI_VERSION, handle, 0};
  if (Call(GTOS_SYS_VM_RELEASE, release) != 0) {
    UnsupportedService();
  }
}

int TryJoin(const Worker& worker, unsigned& result) {
  const GtosThreadJoinRequest request{GTOS_THREAD_ABI_VERSION, worker.id,
                                      reinterpret_cast<unsigned>(&result)};
  return Call(GTOS_SYS_THREAD_TRY_JOIN, request);
}

unsigned CollectLocked(uint32_t current_thread) {
  unsigned count = 0;
  for (auto& worker : workers) {
    if (worker.state != State::kDetached || worker.id == current_thread) {
      continue;
    }
    unsigned result;
    const int status = TryJoin(worker, result);
    if (status == GTOS_THREAD_ERR_BUSY) {
      continue;
    }
    if (status != 0) {
      UnsupportedService();
    }
    ReleaseStack(worker.stack);
    worker = {};
    ++count;
  }
  return count;
}

}  // namespace

int StartThread(size_t stack_bytes, ThreadRoutine routine, void* argument,
                uint32_t* thread_id) {
  if (!routine || !thread_id || stack_bytes > kMaximumStackBytes) {
    return EINVAL;
  }
  const auto info = CurrentProcessInfo();
  if (info.maximum_parallel_threads == 1) {
    return -GTOS_THREAD_ERR_UNSUPPORTED;
  }
  if (stack_bytes == 0) {
    stack_bytes = kDefaultThreadStackBytes;
  }
  if (stack_bytes < 2 * GTOS_VM_PAGE_BYTES) {
    stack_bytes = 2 * GTOS_VM_PAGE_BYTES;
  }
  stack_bytes = (stack_bytes + GTOS_VM_PAGE_BYTES - 1) &
                ~(GTOS_VM_PAGE_BYTES - 1);
  WorkerLock lock;
  CollectLocked(info.thread_id);
  Worker* free = nullptr;
  for (auto& worker : workers) {
    if (worker.state == State::kEmpty) {
      free = &worker;
      break;
    }
  }
  if (!free) {
    return -GTOS_THREAD_ERR_LIMIT;
  }
  GtosVmReserveResult region{};
  const GtosVmReserveRequest reserve{GTOS_VM_ABI_VERSION,
      stack_bytes + 2 * kGuardBytes, GTOS_VM_PAGE_BYTES, 0,
      reinterpret_cast<unsigned>(&region)};
  int status = Call(GTOS_SYS_VM_RESERVE, reserve);
  if (status != 0) {
    return -status;
  }
  if (region.version != GTOS_VM_ABI_VERSION || region.handle == 0 ||
      region.length != reserve.length || region.page_size != GTOS_VM_PAGE_BYTES) {
    UnsupportedService();
  }
  const GtosVmRangeRequest writable{GTOS_VM_ABI_VERSION, region.handle,
                                     kGuardBytes, stack_bytes, GTOS_VM_READ_WRITE};
  status = Call(GTOS_SYS_VM_SET_PERMISSIONS, writable);
  if (status != 0) {
    ReleaseStack(region.handle);
    return -status;
  }
  free->stack = region.handle;
  free->routine = routine;
  free->argument = argument;
  __atomic_store_n(&free->ready, true, __ATOMIC_RELEASE);
  uint32_t id = 0;
  const GtosThreadCreateRequest create{GTOS_THREAD_ABI_VERSION, 0,
      reinterpret_cast<unsigned>(&RunWorker), reinterpret_cast<unsigned>(free),
      region.handle, reinterpret_cast<unsigned>(&id)};
  status = Call(GTOS_SYS_THREAD_CREATE, create);
  if (status != 0) {
    ReleaseStack(region.handle);
    *free = {};
    return -status;
  }
  if (!id) {
    UnsupportedService();
  }
  free->id = id;
  free->state = State::kJoinable;
  *thread_id = id;
  return 0;
}

int JoinThread(uint32_t thread_id, void** result) {
  if (thread_id == CurrentProcessInfo().thread_id) {
    return -GTOS_THREAD_ERR_DEADLOCK;
  }
  Worker* worker;
  {
    WorkerLock lock;
    worker = Find(thread_id);
    if (!worker) {
      return -GTOS_THREAD_ERR_NO_THREAD;
    }
    if (worker->state != State::kJoinable) {
      return EINVAL;
    }
    worker->state = State::kJoining;
  }

  unsigned value = 0;
  int status;
  while ((status = TryJoin(*worker, value)) == GTOS_THREAD_ERR_BUSY) {
    Yield();
  }
  WorkerLock lock;
  if (status != 0) {
    worker->state = State::kJoinable;
    return -status;
  }
  ReleaseStack(worker->stack);
  *worker = {};
  if (result) {
    *result = reinterpret_cast<void*>(value);
  }
  return 0;
}

int DetachThread(uint32_t thread_id) {
  const uint32_t current_thread = CurrentProcessInfo().thread_id;
  WorkerLock lock;
  auto* worker = Find(thread_id);
  if (!worker) {
    return -GTOS_THREAD_ERR_NO_THREAD;
  }
  if (worker->state != State::kJoinable) {
    return EINVAL;
  }
  worker->state = State::kDetached;
  CollectLocked(current_thread);
  return 0;
}

unsigned CollectDetachedThreads() {
  const uint32_t current_thread = CurrentProcessInfo().thread_id;
  WorkerLock lock;
  return CollectLocked(current_thread);
}

}  // namespace gtos::native

_LIBCPP_BEGIN_NAMESPACE_STD

int __libcpp_thread_create(__libcpp_thread_t* thread,
                           void* (*routine)(void*), void* argument) {
  return gtos::native::StartThread(0, routine, argument, thread);
}

int __libcpp_thread_join(__libcpp_thread_t* thread) {
  return thread ? gtos::native::JoinThread(*thread) : EINVAL;
}

int __libcpp_thread_detach(__libcpp_thread_t* thread) {
  return thread ? gtos::native::DetachThread(*thread) : EINVAL;
}

_LIBCPP_END_NAMESPACE_STD

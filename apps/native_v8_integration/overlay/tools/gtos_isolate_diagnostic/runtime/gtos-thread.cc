// Copyright 2026 the GTOS project authors.
// Native lifecycle for the pinned V8 Thread interface.
#include "src/base/platform/platform.h"
#include "src/base/platform/gtos-native-api/vm_abi.h"
#include "src/base/platform/gtos-native-services.h"
#include "thread-context.h"
#include "thread-exit.h"
#include <process/thread_abi.h>

namespace v8 {
namespace base {
class Thread::PlatformData {
 public:
  uint32_t state = 0;  // 0 unstarted, 1 creating, 2 started, 3 joining, 4 joined.
  uint32_t thread = 0;
  uint32_t stack = 0;
};
namespace {
template <class Request>
int Call(unsigned operation, Request& request) {
  asm volatile("int $0x80" : "+a"(operation)
      : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(&request))),
        "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}
void ReleaseStack(Thread::PlatformData* data) {
  if (data->stack == 0) return;
  GtosVmControlRequest release = {GTOS_VM_ABI_VERSION, data->stack, 0};
  if (Call(GTOS_SYS_VM_RELEASE, release) != 0) gtos::UnsupportedService();
  data->stack = 0;
}
[[noreturn]] void Entry(unsigned argument) {
  auto* thread = reinterpret_cast<Thread*>(argument);
  auto* data = thread->data();
  while (__atomic_load_n(&data->state, __ATOMIC_ACQUIRE) == 1) gtos::Yield();
  // Join may already be waiting by the time this worker is first scheduled.
  const uint32_t state = __atomic_load_n(&data->state, __ATOMIC_ACQUIRE);
  if ((state != 2 && state != 3) ||
      gtos::CurrentNativeThreadInfo().thread_id != data->thread)
    gtos::UnsupportedService();
  thread->NotifyStartedAndDispatch();
  gtos_native_exit_with_tls(0);
}
}  // namespace

Thread::Thread(const Options& options)
    : data_(new PlatformData), stack_size_(options.stack_size()),
      priority_(options.priority()), start_semaphore_(nullptr) {
  set_name(options.name());
}
Thread::~Thread() {
  const uint32_t state = __atomic_load_n(&data_->state, __ATOMIC_ACQUIRE);
  if ((state != 0 && state != 4) || data_->stack != 0)
    gtos::UnsupportedService();
  delete start_semaphore_;
  delete data_;
}
void Thread::set_name(const char* name) {
  if (name == nullptr) gtos::UnsupportedService();
  unsigned i = 0;
  for (; i + 1 < sizeof(name_) && name[i] != '\0'; ++i) name_[i] = name[i];
  name_[i] = '\0';
}
bool Thread::Start() {
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&data_->state, &expected, 1U, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return false;
  const auto failed = [this] {
    ReleaseStack(data_);
    data_->thread = 0;
    // The fixed inline StartSynchronously returns directly on Start failure.
    delete start_semaphore_;
    start_semaphore_ = nullptr;
    __atomic_store_n(&data_->state, 0U, __ATOMIC_RELEASE);
    return false;
  };
  const auto info = gtos::CurrentNativeThreadInfo();
  if (info.maximum_parallel_threads != 4 || stack_size_ < 0 ||
      priority_ != Priority::kDefault) return failed();
  uint32_t bytes = stack_size_ == 0 ? 65536U : static_cast<uint32_t>(stack_size_);
  if (bytes < 8192) bytes = 8192;
  if (bytes > UINT32_MAX - 3 * GTOS_VM_PAGE_BYTES) return failed();
  bytes = (bytes + GTOS_VM_PAGE_BYTES - 1) & ~(GTOS_VM_PAGE_BYTES - 1);
  if (bytes / GTOS_VM_PAGE_BYTES > info.maximum_pages) return failed();
  GtosVmReserveResult result = {};
  GtosVmReserveRequest reserve = {GTOS_VM_ABI_VERSION, bytes + 8192, 4096, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result))};
  if (Call(GTOS_SYS_VM_RESERVE, reserve) != 0) return failed();
  data_->stack = result.handle;
  GtosVmRangeRequest commit = {GTOS_VM_ABI_VERSION, data_->stack, 4096, bytes,
                              GTOS_VM_READ_WRITE};
  if (Call(GTOS_SYS_VM_SET_PERMISSIONS, commit) != 0) return failed();
  GtosThreadCreateRequest create = {GTOS_THREAD_ABI_VERSION, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&Entry)),
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), data_->stack,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&data_->thread))};
  if (Call(GTOS_SYS_THREAD_CREATE, create) != 0) return failed();
  if (data_->thread == 0) gtos::UnsupportedService();
  __atomic_store_n(&data_->state, 2U, __ATOMIC_RELEASE);
  return true;
}
void Thread::Join() {
  const uint32_t self = gtos::CurrentNativeThreadInfo().thread_id;
  uint32_t expected = 2;
  if (!__atomic_compare_exchange_n(&data_->state, &expected, 3U, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    gtos::UnsupportedService();
  if (self == data_->thread) gtos::UnsupportedService();
  unsigned exit_code = 0;
  GtosThreadJoinRequest join = {GTOS_THREAD_ABI_VERSION, data_->thread,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&exit_code))};
  int result;
  do {
    result = Call(GTOS_SYS_THREAD_TRY_JOIN, join);
    if (result == GTOS_THREAD_ERR_BUSY) gtos::Yield();
  } while (result == GTOS_THREAD_ERR_BUSY);
  if (result != 0 || exit_code != 0) gtos::UnsupportedService();
  ReleaseStack(data_);
  __atomic_store_n(&data_->state, 4U, __ATOMIC_RELEASE);
}
}  // namespace base
}  // namespace v8

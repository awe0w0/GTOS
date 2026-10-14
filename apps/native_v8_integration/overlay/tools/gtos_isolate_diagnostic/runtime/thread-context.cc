#include "thread-context.h"
#include "src/base/platform/gtos-native-services.h"
namespace v8 { namespace base { namespace gtos {
namespace {
NativeThreadState states[4];
uint32_t context_lock;
class ContextGuard {
 public:
  ContextGuard() {
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(&context_lock, &expected, 1U, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      Yield();
      expected = 0;
    }
  }
  ~ContextGuard() { __atomic_store_n(&context_lock, 0U, __ATOMIC_RELEASE); }
  ContextGuard(const ContextGuard&) = delete;
  ContextGuard& operator=(const ContextGuard&) = delete;
};
}
GtosProcessInfoResult CurrentNativeThreadInfo() {
  GtosProcessInfoResult result = {};
  GtosProcessInfoRequest request = {GTOS_PROCESS_INFO_ABI_VERSION, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result)), sizeof(result)};
  unsigned call = GTOS_SYS_PROCESS_INFO;
  asm volatile("int $0x80" : "+a"(call)
      : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(&request))),
        "c"(sizeof(request)) : "memory", "cc");
  if (call != 0 || result.version != GTOS_PROCESS_INFO_ABI_VERSION ||
      result.process_id == 0 || result.thread_id == 0 ||
      result.process_id > 0x7fffffffU || result.thread_id > 0x7fffffffU ||
      result.user_begin != 0x40000000U || result.user_end != 0xc0000000U ||
      result.stack_begin < result.user_begin || result.stack_end > result.user_end ||
      result.stack_begin >= result.stack_end || result.stack_end - result.stack_begin < 8192 ||
      ((result.stack_begin | result.stack_end) & 4095) != 0 ||
      result.page_bytes != 4096 || result.maximum_pages < 2 || result.maximum_regions == 0 ||
      (result.maximum_parallel_threads != 1 && result.maximum_parallel_threads != 4) ||
      (result.maximum_parallel_threads == 1 && result.thread_id != result.process_id) ||
      result.scheduler_cpu_count != 1) UnsupportedService();
  return result;
}
NativeThreadState& CurrentNativeThreadState() {
  const uint32_t id = CurrentNativeThreadInfo().thread_id;
  ContextGuard guard;
  NativeThreadState* vacant = nullptr;
  for (auto& state : states) {
    if (state.id == id) return state;
    if (state.id == 0 && vacant == nullptr) vacant = &state;
  }
  if (vacant == nullptr) UnsupportedService();
  *vacant = {id, 0, nullptr, nullptr, false};
  return *vacant;
}
void ReleaseNativeThreadState(NativeThreadState& state) {
  const uint32_t id = CurrentNativeThreadInfo().thread_id;
  ContextGuard guard;
  bool registered = false;
  for (auto& candidate : states) if (&candidate == &state) registered = true;
  if (!registered || state.id != id || state.tls_array != nullptr ||
      state.destructors != nullptr || state.cleaning) UnsupportedService();
  state = {};
}
} } }
extern "C" int* __llvm_libc_errno() noexcept {
  return &v8::base::gtos::CurrentNativeThreadState().error;
}

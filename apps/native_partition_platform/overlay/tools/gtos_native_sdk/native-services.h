#ifndef GTOS_NATIVE_SDK_NATIVE_SERVICES_H_
#define GTOS_NATIVE_SDK_NATIVE_SERVICES_H_

#include <stdint.h>

#include <process/abi.h>
#include <process/info_abi.h>

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || \
    defined(__unix__) || defined(_WIN32)
#error These services require the actual GTOS native IA32 ABI
#endif

// Same allocation-free native contract used by the qualified V8 runtime.
// No cached TLS, heap access or platform-thread library is needed here.
namespace gtos::native {

[[noreturn]] inline void UnsupportedService() { __builtin_trap(); }

inline GtosProcessInfoResult CurrentProcessInfo() {
  GtosProcessInfoResult result = {};
  GtosProcessInfoRequest request = {
      GTOS_PROCESS_INFO_ABI_VERSION, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&result)),
      sizeof(result)};
  unsigned call = GTOS_SYS_PROCESS_INFO;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(static_cast<unsigned>(reinterpret_cast<uintptr_t>(&request))),
                 "c"(sizeof(request))
               : "memory", "cc");
  if (call != 0 || result.version != GTOS_PROCESS_INFO_ABI_VERSION ||
      result.process_id == 0 || result.thread_id == 0 ||
      result.process_id > 0x7fffffffU || result.thread_id > 0x7fffffffU ||
      result.user_begin != 0x40000000U || result.user_end != 0xc0000000U ||
      result.stack_begin >= result.stack_end ||
      result.user_begin > result.stack_begin ||
      result.stack_end > result.user_end || result.user_begin >= result.user_end ||
      result.stack_end - result.stack_begin < 8192 ||
      ((result.stack_begin | result.stack_end) & 4095) != 0 ||
      result.page_bytes != 4096 || result.maximum_pages < 2 ||
      result.maximum_regions == 0 ||
      (result.maximum_parallel_threads != 1 && result.maximum_parallel_threads != 4) ||
      (result.maximum_parallel_threads == 1 && result.thread_id != result.process_id) ||
      result.scheduler_cpu_count != 1) {
    UnsupportedService();
  }
  return result;
}

}  // namespace gtos::native

#endif  // GTOS_NATIVE_SDK_NATIVE_SERVICES_H_

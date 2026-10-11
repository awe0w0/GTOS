#ifndef V8_BASE_PLATFORM_GTOS_NATIVE_SERVICES_H_
#define V8_BASE_PLATFORM_GTOS_NATIVE_SERVICES_H_

#include <stdint.h>

#include "src/base/platform/gtos-native-api/abi.h"
#include "src/base/platform/gtos-native-api/info_abi.h"

namespace v8 {
namespace base {
namespace gtos {

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
      result.process_id == 0 || result.thread_id != result.process_id ||
      result.stack_begin >= result.stack_end ||
      result.user_begin > result.stack_begin ||
      result.stack_end > result.user_end || result.user_begin >= result.user_end ||
      result.page_bytes != 4096 || result.maximum_pages == 0 ||
      result.maximum_regions == 0 || result.maximum_parallel_threads != 1 ||
      result.scheduler_cpu_count != 1) {
    UnsupportedService();
  }
  return result;
}

inline void Yield() {
  unsigned call = GTOS_SYS_YIELD;
  asm volatile("int $0x80" : "+a"(call) : "b"(0U), "c"(0U) : "memory", "cc");
  if (call != 0) UnsupportedService();
}

}  // namespace gtos
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_GTOS_NATIVE_SERVICES_H_

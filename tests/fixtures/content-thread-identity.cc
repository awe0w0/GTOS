#include <process/info_abi.h>

#include "base/threading/platform_thread.h"

extern "C" int CheckChromiumThreadIdentity() {
  GtosProcessInfoResult info;
  GtosProcessInfoRequest request = {
      GTOS_PROCESS_INFO_ABI_VERSION, 0,
      reinterpret_cast<unsigned>(&info), sizeof(info)};
  unsigned call = GTOS_SYS_PROCESS_INFO;
  asm volatile("int $0x80" : "+a"(call)
               : "b"(&request), "c"(sizeof(request)) : "cc", "memory");
  if (call || info.version != GTOS_PROCESS_INFO_ABI_VERSION || !info.thread_id)
    return 0;
  return static_cast<unsigned>(base::PlatformThreadBase::CurrentId().raw()) ==
         info.thread_id;
}

// Report only native facts; process limits are not physical RAM capacity.
#include "perfetto/ext/base/android_utils.h"

#include <stdint.h>
#include <stdlib.h>

#include <process/info_abi.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || !defined(__i386__)
#error Native system information requires the GTOS IA32 process info ABI
#endif

#if defined(GTOS_PERFETTO_INFO_TEST)
extern "C" int GtosPerfettoInfoTestCall(unsigned, const void*, unsigned);
#endif

namespace perfetto {
namespace base {

Utsname GetUtsname() {
  // Build identity, not a fabricated kernel release or uname service.
  Utsname value;
  value.sysname = "GTOS";
  value.machine = "i686";
  return value;
}

SystemInfo GetSystemInfo() {
  SystemInfo info;
  info.utsname_info = GetUtsname();
  GtosProcessInfoResult value{};
  const GtosProcessInfoRequest request{
      GTOS_PROCESS_INFO_ABI_VERSION, 0,
      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&value)), sizeof(value)};
#if defined(GTOS_PERFETTO_INFO_TEST)
  const int status = GtosPerfettoInfoTestCall(
      GTOS_SYS_PROCESS_INFO, &request, sizeof(request));
#else
  unsigned operation = GTOS_SYS_PROCESS_INFO;
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(&request)), "c"(sizeof(request))
               : "memory", "cc");
  const int status = static_cast<int>(operation);
#endif
  if (status == 0 && value.version == GTOS_PROCESS_INFO_ABI_VERSION &&
      value.process_id != 0 && value.thread_id != 0 &&
      value.page_bytes == 4096 && value.scheduler_cpu_count == 1) {
    info.page_size = value.page_bytes;
    info.num_cpus = value.scheduler_cpu_count;
  } else {
    PERFETTO_ELOG("Unable to read native process system information");
  }
  // No physical-RAM, configured timezone or kernel-release query is exposed
  // by this ABI. Leave those optionals/strings unknown rather than deriving
  // them from per-process limits, wall time, or the build host.
  return info;
}

std::string GetPerfettoMachineName() {
  const char* name = getenv("PERFETTO_MACHINE_NAME");
  return name ? std::string(name) : GetUtsname().sysname;
}

}  // namespace base
}  // namespace perfetto

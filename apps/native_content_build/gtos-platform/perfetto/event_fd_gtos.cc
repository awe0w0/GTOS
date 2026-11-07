// Event wakeups use their own native service and handle namespace, not files.
#include "perfetto/ext/base/event_fd.h"

#include <stdint.h>
#include <limits.h>
#include <process/event_abi.h>

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || !defined(__i386__)
#error Native event wakeups require the GTOS IA32 event ABI
#endif

#if defined(GTOS_PERFETTO_EVENT_TEST)
extern "C" int GtosPerfettoEventTestCall(unsigned, const void*, unsigned);
#endif

namespace perfetto {
namespace base {
namespace {
int Call(unsigned operation, const void* request, unsigned bytes) {
#if defined(GTOS_PERFETTO_EVENT_TEST)
  return GtosPerfettoEventTestCall(operation, request, bytes);
#else
  asm volatile("int $0x80" : "+a"(operation)
               : "b"(reinterpret_cast<uintptr_t>(request)), "c"(bytes)
               : "memory", "cc");
  return static_cast<int>(operation);
#endif
}
int Control(unsigned operation, PlatformHandle handle) {
  const GtosEventControlRequest request{GTOS_EVENT_ABI_VERSION, static_cast<unsigned>(handle)};
  return Call(operation, &request, sizeof(request));
}
}

int CloseNativeEvent(PlatformHandle handle) { return Control(GTOS_SYS_EVENT_CLOSE, handle); }

EventFd::EventFd() {
  unsigned handle = 0;
  const GtosEventCreateRequest request{GTOS_EVENT_ABI_VERSION, GTOS_EVENT_MANUAL_RESET,
                                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(&handle))};
  PERFETTO_CHECK(Call(GTOS_SYS_EVENT_CREATE, &request, sizeof(request)) == 0);
  PERFETTO_CHECK(handle > 0 && handle <= INT_MAX);
  event_handle_.reset(static_cast<PlatformHandle>(handle));
}

EventFd::~EventFd() = default;

void EventFd::Notify() {
  PERFETTO_CHECK(event_handle_);
  PERFETTO_CHECK(Control(GTOS_SYS_EVENT_SET, event_handle_.get()) == 0);
}

void EventFd::Clear() {
  PERFETTO_CHECK(event_handle_);
  PERFETTO_CHECK(Control(GTOS_SYS_EVENT_RESET, event_handle_.get()) == 0);
}

}  // namespace base
}  // namespace perfetto

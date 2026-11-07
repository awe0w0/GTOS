#include "event_record.h"
#include "perfetto/ext/base/event_fd.h"
#include <process/event_abi.h>
#include <utility>

extern "C" volatile gtos_perfetto_event_probe::Record native_event_record
    __attribute__((section(".data.vm_record"), used, aligned(4))) = {1, GTOS_EVENT_GUEST_MODE, 0, 0, 0, 0};

namespace {
void Write(const char* text) {
  unsigned bytes = 0; while (text[bytes]) ++bytes;
  unsigned operation = 0x4701;
  asm volatile("int $0x80" : "+a"(operation) : "b"(text), "c"(bytes) : "memory", "cc");
}
void Check(bool value) {
  native_event_record.checks = native_event_record.checks + 1;
  if (!value) native_event_record.failures = native_event_record.failures + 1;
}
int Poll(int handle) {
  const unsigned handles[] = {static_cast<unsigned>(handle)};
  const GtosEventWaitRequest request{1, reinterpret_cast<unsigned>(handles), 1, 0};
  native_event_record.waits = native_event_record.waits + 1;
  unsigned operation = GTOS_SYS_EVENT_WAIT;
  asm volatile("int $0x80" : "+a"(operation) : "b"(&request), "c"(sizeof(request)) : "memory", "cc");
  return static_cast<int>(operation);
}
}
extern "C" char* strerror(int) noexcept { return const_cast<char*>("native event request"); }
namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("PEVT REFUSAL\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int NativeEntry() {
  using perfetto::base::EventFd;
  Write("PEVT CONSUMER BEGIN\n");
#if GTOS_EVENT_GUEST_MODE == 0
  for (unsigned iteration = 0; iteration < 8; ++iteration) {
    EventFd first;
    const int id = first.fd();
    Check(Poll(id) == GTOS_EVENT_ERR_TIMEOUT);
    first.Notify();
    Check(Poll(id) == 0);
    Check(Poll(id) == 0);
    first.Notify();
    Check(Poll(id) == 0);
    first.Clear();
    Check(Poll(id) == GTOS_EVENT_ERR_TIMEOUT);
    EventFd moved(std::move(first));
    Check(first.fd() == -1 && moved.fd() == id);
    EventFd other;
    other = std::move(moved);
    Check(moved.fd() == -1 && other.fd() == id);
    other.Notify();
    Check(Poll(id) == 0);
    other.Clear();
    Check(Poll(id) == GTOS_EVENT_ERR_TIMEOUT);
  }
#if defined(GTOS_EVENT_GUEST_WRONG_EXPECTATION)
  Check(false);
#endif
  native_event_record.stage = 2;
  Write(native_event_record.failures ? "PEVT CONSUMER FAIL\n" : "PEVT CONSUMER PASS\n");
  return native_event_record.failures ? 1 : 0;
#else
  EventFd first;
  const int id = first.fd();
  native_event_record.stage = 1;
#if GTOS_EVENT_GUEST_MODE == 1
  Check(id > 0);
  __builtin_trap();
#else
  EventFd moved(std::move(first));
  Check(first.fd() == -1 && moved.fd() == id);
  first.Notify();
#endif
  Write("PEVT FAULT RETURNED UNEXPECTED\n"); return 9;
#endif
}

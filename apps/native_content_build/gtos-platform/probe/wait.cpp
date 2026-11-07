#include "wait_record.h"
#include "perfetto/ext/base/event_fd.h"
#include "perfetto/ext/base/native_wait.h"
#include "perfetto/base/time.h"

extern "C" volatile gtos_perfetto_wait_probe::Record native_wait_record
    __attribute__((section(".data.vm_record"), used, aligned(4))) = {1, GTOS_WAIT_GUEST_MODE, 0, 0, 0, 0};
namespace {
void Write(const char* text) {
  unsigned bytes = 0; while (text[bytes]) ++bytes;
  unsigned op = 0x4701;
  asm volatile("int $0x80" : "+a"(op) : "b"(text), "c"(bytes) : "memory", "cc");
}
void Check(bool value) {
  native_wait_record.checks = native_wait_record.checks + 1;
  if (!value) native_wait_record.failures = native_wait_record.failures + 1;
}
size_t Wait(const perfetto::base::PlatformHandle* handles, size_t count, int ms) {
  native_wait_record.waits = native_wait_record.waits + 1;
  return perfetto::base::WaitForNativeEvents(handles, count, ms);
}
}
extern "C" char* strerror(int) noexcept { return const_cast<char*>("native wait request"); }
namespace perfetto { namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("PWAIT REFUSAL\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
} }

extern "C" int NativeEntry() {
  using namespace perfetto::base;
  Write("PWAIT CONSUMER BEGIN\n");
  {
    EventFd first;
#if GTOS_WAIT_GUEST_MODE == 0
    EventFd second;
    const PlatformHandle ids[] = {first.fd(), second.fd()};
    Check(ids[0] > 0 && ids[1] > 0 && ids[0] != ids[1]);
    first.Notify(); Check(Wait(ids, 2, 0) == 0); Check(Wait(ids, 2, 0) == 0);
    second.Notify(); first.Clear(); Check(Wait(ids, 2, 0) == 1);
    second.Clear(); Check(Wait(ids, 2, 0) == 2);
    int64_t begin = GetWallTimeNs().count();
    Check(Wait(ids, 2, 1) == 2);
    Check(GetWallTimeNs().count() - begin >= 1000000);
    for (unsigned i = 0; i < 2; ++i) {
      begin = GetWallTimeNs().count();
      Check(Wait(ids, 2, 20) == 2);
      Check(GetWallTimeNs().count() - begin >= 20000000);
    }
    begin = GetWallTimeNs().count(); SleepMicroseconds(20001);
    Check(GetWallTimeNs().count() - begin >= 20001000);
    begin = GetWallTimeNs().count(); SleepMicroseconds(1000);
    Check(GetWallTimeNs().count() - begin >= 1000000);
    second.Notify(); Check(Wait(ids, 2, -1) == 1);
#else
    Check(first.fd() > 0); native_wait_record.stage = 1;
    #if GTOS_WAIT_GUEST_MODE == 1
    PlatformHandle bad = 0x7fffffff; Wait(&bad, 1, 0);
    #else
    PlatformHandle ids[17] = {}; Wait(ids, 17, 0);
    #endif
    Write("PWAIT FAULT RETURNED UNEXPECTED\n"); return 9;
#endif
  }
#if GTOS_WAIT_GUEST_MODE == 0
#if defined(GTOS_WAIT_GUEST_WRONG_EXPECTATION)
  Check(false);
#endif
  native_wait_record.stage = 2;
  Write(native_wait_record.failures ? "PWAIT CONSUMER FAIL\n" : "PWAIT CONSUMER PASS\n");
  return native_wait_record.failures ? 1 : 0;
#endif
}

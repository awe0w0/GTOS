#include "perfetto/ext/base/ctrl_c_handler.h"
#include "perfetto/base/logging.h"

namespace {
void Write(const char *text) {
  unsigned bytes = 0;
  while (text[bytes]) ++bytes;
  unsigned eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(text), "d"(bytes) : "memory", "cc");
}
void Handler() { Write("CALLBACK_UNEXPECTED\n"); }
void (*volatile policy)(perfetto::base::CtrlCHandlerFunction) = &perfetto::base::InstallCtrlCHandler;
}

namespace perfetto {
namespace base {
// This is the external logging sink, not an alternative signal implementation.
void LogMessage(LogLev, const char *, int, const char *format, ...) {
  Write("LOG "); Write(format); Write("\n");
}
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() { Write("CRASH_FLUSH\n"); }
#endif
}  // namespace base
}  // namespace perfetto

extern "C" int ControlTestMain() {
  if (!policy) return 9;
#if defined(GTOS_CONTROL_NO_REQUEST)
  Write("CONTROL_OK\n");
#else
  Write("CTRL_REQUEST\n");
#if defined(GTOS_CONTROL_NULL_HANDLER)
  policy(nullptr);
#else
  policy(Handler);
#endif
  Write("RETURNED_UNEXPECTED\n");
#endif
  return 0;
}

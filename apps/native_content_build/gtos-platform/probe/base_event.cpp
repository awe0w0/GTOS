#include "memory_record.h"

#include "base/logging.h"
#include "base/synchronization/waitable_event.h"

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {1, 0, 0, 0, 0, 0, 0};
}

namespace {

void Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok) {
    native_memory_record.failures = native_memory_record.failures + 1;
  }
}

}  // namespace

extern "C" int NativeEntry() {
  using Event = base::WaitableEvent;
  {
    Event manual(Event::ResetPolicy::MANUAL, Event::InitialState::NOT_SIGNALED);
    Event automatic(Event::ResetPolicy::AUTOMATIC, Event::InitialState::SIGNALED);
    Check(!manual.IsSignaled());
    Check(automatic.IsSignaled());
    Check(!automatic.IsSignaled());
    manual.Signal();
    Check(manual.IsSignaled());
    Check(manual.IsSignaled());
    manual.Reset();
#ifdef GTOS_BASE_EVENT_GUEST_WRONG_EXPECTATION
    Check(manual.IsSignaled());
#else
    Check(!manual.IsSignaled());
#endif
    automatic.Signal();
    automatic.Signal();
    Check(automatic.IsSignaled());
    Check(!automatic.IsSignaled());
    automatic.Signal();
    automatic.Reset();
    Check(!automatic.IsSignaled());
  }
  {
    Event again(Event::ResetPolicy::AUTOMATIC, Event::InitialState::NOT_SIGNALED);
    Check(!again.IsSignaled());
  }

  // Three native write chunks and a separately appended newline.
  char message[605];
  for (unsigned i = 0; i + 1 < sizeof(message); ++i) {
    message[i] = static_cast<char>('A' + i % 26);
  }
  message[sizeof(message) - 1] = '\0';
  logging::RawLog(logging::LOGGING_INFO, message);

  native_memory_record.stage = 2;
  return native_memory_record.failures != 0;
}

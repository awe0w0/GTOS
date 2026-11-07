#include "perfetto/ext/base/event_fd.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/pipe.h"
#include <process/event_abi.h>
#include <new>
#include <string.h>
#include <utility>

namespace {
unsigned failures, calls, created, closed, notified, cleared;
unsigned next_id = 100;
struct Event { unsigned id; bool signal; } events[4];
void Write(const char* text) {
  unsigned bytes = 0; while (text[bytes]) ++bytes;
  unsigned operation = 4;
  asm volatile("int $0x80" : "+a"(operation) : "b"(1), "c"(text), "d"(bytes) : "memory", "cc");
}
void Check(bool value) { if (!value) { ++failures; Write("CHECK_FAILED\n"); } }
Event* Find(unsigned id) { for (auto& event : events) if (event.id == id) return &event; return nullptr; }
}

// String allocations are not expected: the refusal cases use native SSO names.
void* operator new(size_t) { Write("ALLOCATION_UNEXPECTED\n"); __builtin_trap(); }
void operator delete(void*) noexcept { Write("DELETE_UNEXPECTED\n"); __builtin_trap(); }
void operator delete(void* value, size_t) noexcept { operator delete(value); }
void* operator new(size_t bytes, std::align_val_t) { return operator new(bytes); }
void operator delete(void* value, size_t, std::align_val_t) noexcept { operator delete(value); }
extern "C" char* strerror(int) noexcept { return const_cast<char*>("test event error"); }
extern "C" int close(int) { Write("WRONG_FILE_CLOSE\n"); __builtin_trap(); }
extern "C" void* memmove(void* out, const void* in, size_t bytes) noexcept {
  auto* to = static_cast<unsigned char*>(out); const auto* from = static_cast<const unsigned char*>(in);
  if (to < from) { for (size_t i = 0; i < bytes; ++i) to[i] = from[i]; }
  else { while (bytes) { --bytes; to[bytes] = from[bytes]; } }
  return out;
}
extern "C" [[noreturn]] void abort() noexcept { Write("ABORT_UNEXPECTED\n"); __builtin_trap(); }
extern "C" void __assert_fail(const char*, const char*, unsigned, const char*) { abort(); }
_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept { abort(); }
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) {
#if GTOS_EVENT_CASE >= 8
  if (calls) Write("UNEXPECTED_NATIVE_CALL\n");
  Write("CAPABILITY_UNAVAILABLE\n");
#else
  if (GTOS_EVENT_CASE == 7 && calls != 1) Write("UNEXPECTED_NATIVE_CALL\n");
  Write("EVENT_INVALID\n");
#endif
}
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int GtosPerfettoEventTestCall(unsigned operation, const void* pointer, unsigned bytes) {
  ++calls;
  if (operation == GTOS_SYS_EVENT_CREATE) {
    const auto& request = *static_cast<const GtosEventCreateRequest*>(pointer);
    Check(bytes == sizeof(request) && request.version == 1 && request.flags == GTOS_EVENT_MANUAL_RESET && request.result_va);
#if GTOS_EVENT_CASE == 1
    return GTOS_EVENT_ERR_UNAVAILABLE;
#elif GTOS_EVENT_CASE == 2
    *reinterpret_cast<unsigned*>(request.result_va) = 0; return 0;
#elif GTOS_EVENT_CASE == 3
    *reinterpret_cast<unsigned*>(request.result_va) = 0x80000000U; return 0;
#endif
    for (auto& event : events) if (!event.id) {
      event.id = next_id++; event.signal = false; ++created;
      *reinterpret_cast<unsigned*>(request.result_va) = event.id; return 0;
    }
    return GTOS_EVENT_ERR_LIMIT;
  }
  const auto& request = *static_cast<const GtosEventControlRequest*>(pointer);
  Check(bytes == sizeof(request) && request.version == 1);
  Event* event = Find(request.handle);
  Check(event != nullptr);
  if (!event) return GTOS_EVENT_ERR_BAD_HANDLE;
  if (operation == GTOS_SYS_EVENT_SET) {
    if (GTOS_EVENT_CASE == 4) return GTOS_EVENT_ERR_PERMISSION;
    event->signal = true; ++notified; return 0;
  }
  if (operation == GTOS_SYS_EVENT_RESET) {
    if (GTOS_EVENT_CASE == 5) return GTOS_EVENT_ERR_BAD_HANDLE;
    event->signal = false; ++cleared; return 0;
  }
  Check(operation == GTOS_SYS_EVENT_CLOSE);
  if (GTOS_EVENT_CASE == 6) return GTOS_EVENT_ERR_BAD_HANDLE;
  event->id = 0; event->signal = false; ++closed; return 0;
}

extern "C" int EventTestMain() {
  using namespace perfetto::base;
#if GTOS_EVENT_CASE != 0
  Write("INVALID_REQUEST\n");
#if GTOS_EVENT_CASE >= 8 && GTOS_EVENT_CASE <= 11
  (void)Pipe::Create(static_cast<Pipe::Flags>(GTOS_EVENT_CASE - 8));
#elif GTOS_EVENT_CASE >= 12
  (void)IsSameFile("/same", GTOS_EVENT_CASE == 12 ? "/same" : "/other");
#else
  {
    EventFd event;
    if (GTOS_EVENT_CASE == 4) event.Notify();
    if (GTOS_EVENT_CASE == 5) event.Clear();
    if (GTOS_EVENT_CASE == 7) { EventFd moved(std::move(event)); event.Notify(); }
  }
#endif
  Write("RETURNED_UNEXPECTED\n"); return 9;
#else
  for (unsigned iteration = 0; iteration < 8; ++iteration) {
    {
      EventFd first;
      const auto id = first.fd();
      Check(id >= 100 && Find(id) && !Find(id)->signal);
      first.Notify(); first.Notify(); Check(Find(id)->signal);
      first.Clear(); Check(!Find(id)->signal);
      EventFd moved(std::move(first));
      Check(first.fd() == -1 && moved.fd() == id);
      EventFd other;
      const auto replaced = other.fd();
      Check(replaced != id);
      other = std::move(moved);
      Check(!Find(replaced) && moved.fd() == -1 && other.fd() == id);
      other.Notify(); Check(Find(id)->signal);
      other.Clear(); Check(!Find(id)->signal);
    }
    Check(created == (iteration + 1) * 2 && closed == created);
    Check(notified == (iteration + 1) * 3 && cleared == (iteration + 1) * 2);
    for (const auto& event : events) Check(event.id == 0);
  }
  Pipe empty;
  Pipe moved(std::move(empty));
  Check(!moved.rd && !moved.wr && !empty.rd && !empty.wr);
#if defined(GTOS_EVENT_WRONG_EXPECTATION)
  Check(created == 0);
#endif
  Write(failures ? "EVENT_CHECK_FAIL\n" : "EVENT_CHECK_PASS\n");
  return failures ? 1 : 0;
#endif
}

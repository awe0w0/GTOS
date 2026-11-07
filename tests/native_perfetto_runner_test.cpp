#include "perfetto/ext/base/unix_task_runner.h"
#include "perfetto/ext/base/lock_free_task_runner.h"
#include "perfetto/ext/base/native_wait.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/watchdog.h"
#include <process/abi.h>
#include <process/clock_abi.h>
#include <process/event_abi.h>
#include <errno.h>
#include <limits.h>
#include <new>
#include <string.h>

namespace {
unsigned failures, checks, calls, wait_calls, yield_calls, live_allocations;
unsigned creates, closes, next_id = 100;
unsigned thread_id = 1;
void (*wait_hook)(void*);
void* wait_hook_argument;
int64_t now_ns = 1000000000;
struct Event { unsigned id; bool signal; } events[32];
alignas(16) unsigned char heap[256][32768];
bool used[256];
void Write(const char* text) {
  unsigned bytes = 0; while (text[bytes]) ++bytes;
  unsigned op = 4;
  asm volatile("int $0x80" : "+a"(op) : "b"(1), "c"(text), "d"(bytes) : "memory", "cc");
}
void Check(bool value, unsigned line = __builtin_LINE()) {
  ++checks;
  if (value) return;
  ++failures; Write("CHECK_FAILED ");
  char text[10];
  for (unsigned i = 0; i < 8; ++i) text[i] = "0123456789ABCDEF"[(line >> (28 - i * 4)) & 15];
  text[8] = '\n'; text[9] = 0; Write(text);
}
Event* Find(unsigned id) { for (auto& e : events) if (e.id == id) return &e; return nullptr; }
}

void* operator new(size_t bytes) {
  if (bytes > sizeof(heap[0])) { Write("ALLOCATION_TOO_LARGE\n"); __builtin_trap(); }
  for (unsigned i = 0; i < 256; ++i) if (!used[i]) {
    used[i] = true; ++live_allocations; return heap[i];
  }
  Write("ALLOCATION_EXHAUSTED\n"); __builtin_trap();
}
void operator delete(void* p) noexcept {
  if (!p) return;
  for (unsigned i = 0; i < 256; ++i) if (p == heap[i]) {
    Check(used[i]); used[i] = false; --live_allocations; return;
  }
  Check(false);
}
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void* operator new(size_t n, std::align_val_t a) { Check(static_cast<unsigned>(a) <= 16); return operator new(n); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { operator delete(p); }
extern "C" {
char* strerror(int) noexcept { return const_cast<char*>("test error"); }
int close(int) { Write("WRONG_FILE_CLOSE\n"); __builtin_trap(); }
int clock_gettime(clockid_t id, timespec* out) noexcept {
  Check(id == CLOCK_MONOTONIC);
  *out = {now_ns / 1000000000, now_ns % 1000000000}; return 0;
}
void* memmove(void* out, const void* in, size_t n) noexcept {
  auto* a = static_cast<unsigned char*>(out); const auto* b = static_cast<const unsigned char*>(in);
  if (a < b) for (size_t i = 0; i < n; ++i) a[i] = b[i];
  else while (n) { --n; a[n] = b[n]; }
  return out;
}
[[noreturn]] void abort() noexcept { Write("ABORT_UNEXPECTED\n"); __builtin_trap(); }
void __assert_fail(const char*, const char*, unsigned, const char*) { abort(); }
void __cxa_pure_virtual() { abort(); }
int __cxa_guard_acquire(unsigned long long* state) { return *state == 0; }
void __cxa_guard_release(unsigned long long* state) { *state = 1; }
int GtosPerfettoEventTestCall(unsigned op, const void* pointer, unsigned bytes) {
  ++calls;
  if (op == GTOS_SYS_EVENT_CREATE) {
    const auto& r = *static_cast<const GtosEventCreateRequest*>(pointer);
    Check(bytes == sizeof(r) && r.version == 1 && r.flags == GTOS_EVENT_MANUAL_RESET && r.result_va);
    for (auto& e : events) if (!e.id) {
      e = {next_id++, false}; ++creates;
      *reinterpret_cast<unsigned*>(r.result_va) = e.id; return 0;
    }
    return GTOS_EVENT_ERR_LIMIT;
  }
  const auto& r = *static_cast<const GtosEventControlRequest*>(pointer);
  Check(bytes == sizeof(r) && r.version == 1);
  auto* e = Find(r.handle); Check(e != nullptr);
  if (!e) return GTOS_EVENT_ERR_BAD_HANDLE;
  if (op == GTOS_SYS_EVENT_SET) { e->signal = true; return 0; }
  if (op == GTOS_SYS_EVENT_RESET) { e->signal = false; return 0; }
  if (op == GTOS_SYS_EVENT_CLOSE) { e->id = 0; ++closes; return 0; }
  Check(false); return -22;
}
int GtosPerfettoWaitTestCall(unsigned op, const void* pointer, unsigned bytes) {
  ++calls;
  if (op == GTOS_SYS_YIELD) {
    Check(!pointer && !bytes); ++yield_calls;
#if GTOS_RUNNER_CASE == 7
    --now_ns;
#elif GTOS_RUNNER_CASE == 8
    return -38;
#else
    now_ns += 10000000;
#endif
    return 0;
  }
  ++wait_calls;
  if (wait_hook) {
    auto hook = wait_hook; wait_hook = nullptr;
    thread_id = 2; hook(wait_hook_argument); thread_id = 1;
  }
  const auto& r = *static_cast<const GtosEventWaitRequest*>(pointer);
  Check(op == GTOS_SYS_EVENT_WAIT && bytes == sizeof(r) && r.version == 1 &&
        r.count && r.count <= 16 && r.handles_va && r.timeout_ticks <= GTOS_EVENT_TIMEOUT_LIMIT);
#if GTOS_RUNNER_CASE == 1
  return -9;
#elif GTOS_RUNNER_CASE == 2
  return static_cast<int>(r.count);
#elif GTOS_RUNNER_CASE == 9
  now_ns -= 1; return GTOS_EVENT_ERR_TIMEOUT;
#endif
  const auto* ids = reinterpret_cast<const unsigned*>(r.handles_va);
  for (unsigned i = 0; i < r.count; ++i) if (!Find(ids[i])) return -9;
  for (unsigned i = 0; i < r.count; ++i) if (Find(ids[i])->signal) return static_cast<int>(i);
  if (r.timeout_ticks) {
    // First response models the partially elapsed tick; the deadline must be
    // checked again rather than accepting a timeout as sufficient elapsed time.
    if (wait_calls == 1) now_ns += 1;
    else now_ns += static_cast<int64_t>(r.timeout_ticks) * 10000000;
  }
  return GTOS_EVENT_ERR_TIMEOUT;
}
}

_LIBCPP_BEGIN_NAMESPACE_STD
__libcpp_thread_id __libcpp_thread_get_current_id() { return thread_id; }
bool __libcpp_thread_id_equal(__libcpp_thread_id a, __libcpp_thread_id b) { return a == b; }
void __libcpp_thread_yield() { ++yield_calls; }
int __libcpp_mutex_lock(__libcpp_mutex_t* m) { Check(!m->owner); m->owner = thread_id; return 0; }
int __libcpp_mutex_unlock(__libcpp_mutex_t* m) { Check(m->owner == thread_id); m->owner = 0; return 0; }
int __libcpp_mutex_destroy(__libcpp_mutex_t* m) { Check(!m->owner); return 0; }
bool __libcpp_mutex_trylock(__libcpp_mutex_t* m) { if (m->owner) return false; m->owner = 1; return true; }
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
[[noreturn]] void __throw_system_error(int, const char*) { abort(); }
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept { abort(); }
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

namespace perfetto { namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("WAIT_REFUSAL\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
} }

template <class Runner> void Exercise() {
  using namespace perfetto::base;
  for (unsigned repeat = 0; repeat < 3; ++repeat) {
    const unsigned allocations_before = live_allocations;
    {
      Runner runner;
      Check(runner.RunsTasksOnCurrentThread() && runner.IsIdleForTesting());
      unsigned sequence = 0;
      runner.PostTask([&] { Check(sequence++ == 0); runner.PostTask([&] { Check(sequence++ == 1); }); });
      runner.PostDelayedTask([&] { Check(sequence++ == 2); runner.Quit(); }, 25);
      runner.Run(); Check(sequence == 3 && runner.IsIdleForTesting());
      runner.PostTask([&] { ++sequence; runner.Quit(); });
      runner.Run(); Check(sequence == 4);
      const int64_t long_begin = now_ns;
      runner.PostDelayedTask([&] { runner.Quit(); }, UINT_MAX);
      runner.Run(); Check(now_ns - long_begin >= 4294967295000000LL);
      EventFd first, second;
      unsigned callbacks = 0;
      runner.AddFileDescriptorWatch(first.fd(), [&] { first.Clear(); ++callbacks; });
      runner.AddFileDescriptorWatch(second.fd(), [&] { second.Clear(); ++callbacks; });
      first.Notify(); second.Notify();
      runner.PostDelayedTask([&] { Check(callbacks == 2); runner.Quit(); }, 20);
      runner.Run(); Check(callbacks == 2);
      runner.RemoveFileDescriptorWatch(first.fd());
      runner.RemoveFileDescriptorWatch(second.fd());
      first.Notify();
      runner.PostDelayedTask([&] { Check(callbacks == 2); runner.Quit(); }, 10);
      runner.Run();
      // Serialize a remote API call inside the injected wait boundary. This
      // checks a removed ready handle without claiming a real thread race.
      runner.AddFileDescriptorWatch(first.fd(), [&] { Check(false); });
      struct Removal { Runner* runner; PlatformHandle handle; } removal{&runner, first.fd()};
      wait_hook_argument = &removal;
      wait_hook = [](void* p) {
        auto* r = static_cast<Removal*>(p); r->runner->RemoveFileDescriptorWatch(r->handle);
      };
      runner.PostDelayedTask([&] { runner.Quit(); }, 10); runner.Run();
      Check(!wait_hook && callbacks == 2);
      unsigned count = 0;
      for (unsigned i = 0; i < 1200; ++i) runner.PostTask([&, i] { Check(count++ == i); });
      runner.PostTask([&] { runner.Quit(); }); runner.Run();
      Check(count == 1200 && runner.IsIdleForTesting());
      // The maximum includes the internal wakeup event.
      EventFd additional[15];
      for (auto& event : additional) runner.AddFileDescriptorWatch(event.fd(), [] {});
      runner.PostTask([&] { runner.Quit(); }); runner.Run();
      for (auto& event : additional) runner.RemoveFileDescriptorWatch(event.fd());
    }
    Check(live_allocations == allocations_before);
  }
}

extern "C" int RunnerTestMain() {
  using namespace perfetto::base;
#if GTOS_RUNNER_CASE != 0
  Write("INVALID_REQUEST\n");
  PlatformHandle ids[17] = {};
#if GTOS_RUNNER_CASE == 3
  WaitForNativeEvents(ids, 0, 0);
#elif GTOS_RUNNER_CASE == 4
  WaitForNativeEvents(ids, 17, 0);
#elif GTOS_RUNNER_CASE == 5
  ids[0] = -1; WaitForNativeEvents(ids, 1, 0);
#elif GTOS_RUNNER_CASE == 6
  ids[0] = 1; WaitForNativeEvents(ids, 1, -2);
#elif GTOS_RUNNER_CASE == 7 || GTOS_RUNNER_CASE == 8
  SleepMicroseconds(1);
#elif GTOS_RUNNER_CASE == 10 || GTOS_RUNNER_CASE == 11
  EventFd extra[16];
  #if GTOS_RUNNER_CASE == 10
  UnixTaskRunner runner;
  #else
  LockFreeTaskRunner runner;
  #endif
  for (auto& event : extra) runner.AddFileDescriptorWatch(event.fd(), [] {});
#else
  EventFd e; auto id = e.fd(); WaitForNativeEvents(&id, 1, 1);
#endif
  Write("RETURNED_UNEXPECTED\n"); return 8;
#else
  // Upstream intentionally retains this process-lifetime singleton. Assert
  // its exact one allocation, then require all queue allocations to balance.
  Check(live_allocations == 0);
  auto* watchdog = Watchdog::GetInstance();
  Check(watchdog && live_allocations == 1 && Watchdog::GetInstance() == watchdog);
  {
    Check(!IsTty(-1) && !IsTty(0) && !IsTty(1) && !IsTty(INT_MAX));
    EventFd e; auto id = e.fd();
    Check(WaitForNativeEvents(&id, 1, 0) == 1);
    wait_calls = 0; const int64_t begin = now_ns;
    Check(WaitForNativeEvents(&id, 1, 1) == 1 && now_ns - begin >= 1000000 && wait_calls == 2);
    e.Notify(); Check(WaitForNativeEvents(&id, 1, -1) == 0); e.Clear();
    const unsigned yields = yield_calls; SleepMicroseconds(0); Check(yield_calls == yields);
    const int64_t before = now_ns; SleepMicroseconds(20001); Check(now_ns - before == 30000000);
    const int64_t large = now_ns; SleepMicroseconds(UINT_MAX); Check(now_ns - large >= 4294967295000LL);
    Check(WaitForNativeEvents(&id, 1, INT_MAX) == 1);
  }
  Exercise<UnixTaskRunner>();
  Exercise<LockFreeTaskRunner>();
  Check(live_allocations == 1 && creates == closes && wait_calls > 1000);
#if defined(GTOS_RUNNER_WRONG_EXPECTATION)
  Check(creates == 0);
#endif
  Write(failures ? "RUNNER_CHECK_FAIL\n" : "RUNNER_CHECK_PASS\n");
  return failures ? 1 : 0;
#endif
}

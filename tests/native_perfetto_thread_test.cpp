#include "perfetto/base/thread_utils.h"
#include "perfetto/ext/base/thread_checker.h"

extern "C" {
int* __llvm_libc_errno() noexcept { static int value; return &value; }
char* strerror(int) noexcept { return const_cast<char*>("test errno"); }
}

namespace {
unsigned owner = 7;
unsigned reads = 0;
unsigned checks = 0;
unsigned failed = 0;
void Write(const char* text) {
  unsigned bytes = 0;
  while (text[bytes]) ++bytes;
  unsigned eax = 4;
  asm volatile("int $0x80" : "+a"(eax) : "b"(1), "c"(text), "d"(bytes) : "memory", "cc");
}
void Check(bool value) {
  ++checks;
  if (!value) { ++failed; Write("CHECK_FAILED\n"); }
}
}

// Only the native thread-identity boundary is injected. The consumer and its
// atomic detach/copy/rebind logic are the actual Perfetto implementation.
_LIBCPP_BEGIN_NAMESPACE_STD
__libcpp_thread_id __libcpp_thread_get_current_id() {
  ++reads;
  return owner;
}
_LIBCPP_END_NAMESPACE_STD

namespace perfetto {
namespace base {
void LogMessage(LogLev, const char*, int, const char*, ...) { Write("INVALID_OWNER\n"); }
#if PERFETTO_ENABLE_LOG_RING_BUFFER()
void MaybeSerializeLastLogsForCrashReporting() {}
#endif
}
}

extern "C" int ThreadTestMain() {
  using perfetto::base::ThreadChecker;
  using perfetto::base::GetThreadId;
  static_assert(sizeof(perfetto::base::PlatformThreadId) == 4, "native thread identity");
#if defined(GTOS_THREAD_INVALID_OWNER)
  owner = 0;
  Write("INVALID_REQUEST\n");
  (void)GetThreadId();
  Write("RETURNED_UNEXPECTED\n");
  return 8;
#else
  Check(GetThreadId() == 7);
  ThreadChecker original;
  Check(original.CalledOnValidThread());
  owner = 9;
  Check(GetThreadId() == 9);
  Check(!original.CalledOnValidThread());
  ThreadChecker copied(original);
  Check(!copied.CalledOnValidThread());
  ThreadChecker assigned;
  Check(assigned.CalledOnValidThread());
  assigned = original;
  Check(!assigned.CalledOnValidThread());
  original.DetachFromThread();
  Check(original.CalledOnValidThread());
  Check(original.CalledOnValidThread());
  owner = 7;
  Check(!original.CalledOnValidThread());
  Check(copied.CalledOnValidThread());
  Check(assigned.CalledOnValidThread());
  for (unsigned index = 0; index < 64; ++index) {
    owner = 100 + index;
    original.DetachFromThread();
    Check(original.CalledOnValidThread());
    ++owner;
    Check(!original.CalledOnValidThread());
  }
  owner = 0xFFFFFFFF;
  original.DetachFromThread();
  Check(GetThreadId() == owner);
  Check(original.CalledOnValidThread());
  owner = 7;
  Check(!original.CalledOnValidThread());
  Check(reads == 145);
  Check(checks == 144);
#if defined(GTOS_THREAD_WRONG_EXPECTATION)
  Check(GetThreadId() == 8);
#endif
  Write(failed ? "THREAD_CHECK_FAIL\n" : "THREAD_CHECK_PASS\n");
  return failed ? 1 : 0;
#endif
}


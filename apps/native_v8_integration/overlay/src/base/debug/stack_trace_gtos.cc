// Copyright 2026 the GTOS project authors.
// Native IA32 diagnostic stack reporting for the pinned V8 interface.
#include "src/base/debug/stack_trace.h"

#include <errno.h>
#include <stdio.h>
#include <ostream>

#include "src/base/platform/platform.h"

#if !V8_OS_GTOS || V8_OS_POSIX || V8_OS_LINUX || !defined(__i386__) || \
    !defined(GTOS_ISOLATE_COMPILE_DIAGNOSTIC)
#error This backend requires the independent GTOS IA32 diagnostic target
#endif

namespace v8 {
namespace base {
namespace debug {

bool EnableInProcessStackDumping() {
  // Kernel fault reports remain available. This profile has no asynchronous
  // V8 signal handler or unwinder to install.
  return false;
}

void DisableSignalStackDump() {
  // EnableInProcessStackDumping never installs a handler in this profile.
}

__attribute__((noinline)) StackTrace::StackTrace() {
  // Exact immediate caller PC only. The target omits frame pointers and unwind
  // tables, so following a frame chain would read unvalidated stack addresses.
  trace_[0] = __builtin_extract_return_addr(__builtin_return_address(0));
  count_ = trace_[0] == nullptr ? 0 : 1;
}

void StackTrace::Print() const {
  if (stderr == nullptr) {
    errno = EBADF;
    return;
  }
  for (size_t i = 0; i < count_; ++i) {
    OS::PrintError("#%u %p\n", static_cast<unsigned>(i), trace_[i]);
  }
}

void StackTrace::OutputToStream(std::ostream* os) const {
  // Raw addresses only: symbols and additional caller frames are unavailable.
  // write() preserves the caller's numeric base, precision and fill settings.
  for (size_t i = 0; i < count_; ++i) {
    char line[32];
    const int length = OS::SNPrintF(line, sizeof(line), "#%u %p\n",
                                    static_cast<unsigned>(i), trace_[i]);
    if (length < 0) return;
    os->write(line, length);
  }
}

}  // namespace debug
}  // namespace base
}  // namespace v8

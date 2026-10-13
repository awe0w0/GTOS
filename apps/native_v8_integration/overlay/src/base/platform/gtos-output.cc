// Copyright 2026 the GTOS project authors.
// Native FILE output for the pinned V8 OS interface.
#include "src/base/platform/platform.h"

#include <stdarg.h>
#include <stdio.h>

#if !V8_OS_GTOS || V8_OS_POSIX || V8_OS_LINUX || !defined(__i386__) || \
    !defined(GTOS_ISOLATE_COMPILE_DIAGNOSTIC)
#error This backend requires the independent GTOS IA32 diagnostic target
#endif

namespace v8 {
namespace base {

void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}

void OS::VPrint(const char* format, va_list args) {
  vprintf(format, args);
}

void OS::FPrint(FILE* out, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(out, format, args);
  va_end(args);
}

void OS::VFPrint(FILE* out, const char* format, va_list args) {
  vfprintf(out, format, args);
}

void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
  fflush(stderr);
}

void OS::VPrintError(const char* format, va_list args) {
  vfprintf(stderr, format, args);
}

}  // namespace base
}  // namespace v8

// Copyright 2026 the GTOS project authors.
// Native regular-file open for the pinned V8 OS interface.
#include "src/base/platform/platform.h"

#include <errno.h>
#include <stdio.h>

#if !V8_OS_GTOS || V8_OS_POSIX || V8_OS_LINUX || !defined(__i386__) || \
    !defined(GTOS_ISOLATE_COMPILE_DIAGNOSTIC)
#error This backend requires the independent GTOS IA32 diagnostic target
#endif

namespace v8 {
namespace base {

int OS::GetLastError() { return errno; }

const char* const OS::LogFileOpenMode = "w+";

FILE* OS::OpenTemporaryFile() { return tmpfile(); }

FILE* OS::FOpen(const char* path, const char* mode) {
  // NativeFiles/FileStore admits only regular LittleFS files through fopen.
  // Directory handles use a separate ABI and cannot become a FILE here.
  return fopen(path, mode);
}

}  // namespace base
}  // namespace v8

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <array>
#include <string_view>

#include "base/compiler_specific.h"

extern "C" int GtosContentShellMain(int argc, const char** argv);
extern "C" int gtos_native_environment_initialize() noexcept;
#if defined(GTOS_CONTENT_PARTITION_HEAP)
extern "C" int gtos_content_heap_initialize() noexcept;
#else
extern "C" int gtos_native_heap_enable_large_allocations() noexcept;
#endif

using Initializer = void (*)();
extern "C" Initializer __init_array_start[], __init_array_end[];

namespace {

constexpr unsigned kMaximumExtraArguments = 16;
using Arguments = std::array<const char*, kMaximumExtraArguments + 2>;

int ReadLaunchArguments(Arguments& argv) {
  argv[0] = "content_shell";
  const char* count = getenv("GTOS_CONTENT_ARG_COUNT");
  if (!count) {
    return 1;
  }
  if (!*count) {
    return 0;
  }

  unsigned extra = 0;
  for (const char digit : std::string_view(count)) {
    if (digit < '0' || digit > '9') {
      return 0;
    }
    extra = extra * 10 + (digit - '0');
    if (extra > kMaximumExtraArguments) {
      return 0;
    }
  }

  // The kernel's immutable launch environment carries each argument verbatim.
  char key[] = "GTOS_CONTENT_ARG_00";
  for (unsigned i = 1; i <= extra; ++i) {
    key[sizeof(key) - 3] = static_cast<char>('0' + i / 10);
    key[sizeof(key) - 2] = static_cast<char>('0' + i % 10);
    argv[i] = getenv(key);
    if (!argv[i]) {
      return 0;
    }
  }
  return static_cast<int>(extra + 1);
}

}  // namespace

extern "C" {

// The shared GTOS runtime records these at startup and during exit.
volatile unsigned native_v8_integration_stage = 0;
volatile unsigned native_v8_integration_result = 0xffffffffU;

[[noreturn]] void NativeEntry() {
  native_v8_integration_stage = 1;
#if defined(GTOS_CONTENT_PARTITION_HEAP)
  if (gtos_content_heap_initialize() != 0 ||
      gtos_native_environment_initialize() != 0) {
#else
  if (gtos_native_environment_initialize() != 0 ||
      gtos_native_heap_enable_large_allocations() != 0) {
#endif
    __builtin_trap();
  }
  Arguments argv{};
  const int argc = ReadLaunchArguments(argv);
  if (argc == 0) {
    exit(64);
  }

  // SAFETY: The GTOS linker script bounds one contiguous initializer array.
  UNSAFE_BUFFERS({
    for (Initializer* init = __init_array_start; init != __init_array_end;
         ++init) {
      (*init)();
    }
  });
  native_v8_integration_stage = 2;

  exit(GtosContentShellMain(argc, argv.data()));
}

}  // extern "C"

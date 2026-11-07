// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "base/compiler_specific.h"

extern "C" int GtosContentShellMain(int argc, const char** argv);
extern "C" int gtos_native_heap_enable_large_allocations() noexcept;

using Initializer = void (*)();
extern "C" Initializer __init_array_start[], __init_array_end[];

extern "C" {

// The shared GTOS runtime records these at startup and during exit.
volatile unsigned native_v8_integration_stage = 0;
volatile unsigned native_v8_integration_result = 0xffffffffU;

[[noreturn]] void NativeEntry() {
  native_v8_integration_stage = 1;
  if (gtos_native_heap_enable_large_allocations() != 0) {
    __builtin_trap();
  }

  // SAFETY: The GTOS linker script bounds one contiguous initializer array.
  UNSAFE_BUFFERS({
    for (Initializer* init = __init_array_start; init != __init_array_end;
         ++init) {
      (*init)();
    }
  });
  native_v8_integration_stage = 2;

  // The native loader enters on a fresh stack and supplies no argument vector.
  const char* argv[] = {"content_shell", nullptr};
  exit(GtosContentShellMain(1, argv));
}

}  // extern "C"

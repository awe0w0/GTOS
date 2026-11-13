// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "process/abi.h"

extern "C" void _Exit(int status) noexcept {
  unsigned call = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(call)
               : "b"(static_cast<unsigned>(status)) : "memory", "cc");
  __builtin_trap();
}

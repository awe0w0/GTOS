// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <__external_threading>
#include <gtos/once.h>

extern "C" void gtos_call_once(uint32_t* flag, void (*function)(void)) {
  if (std::__libcpp_execute_once(flag, function) != 0) {
    __builtin_trap();
  }
}

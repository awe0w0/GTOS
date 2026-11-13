// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/debugger.h"

#include "base/immediate_crash.h"

namespace base::debug {

bool BeingDebugged() {
  // The native process ABI has no debugger attach interface.
  return false;
}

void BreakDebuggerAsyncSafe() {
  base::ImmediateCrash();
}

void VerifyDebugger() {}

}  // namespace base::debug

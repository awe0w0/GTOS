// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <gtos/diagnostic.h>

#include "base/compiler_specific.h"

extern "C" void perror(const char* message) noexcept {
  const int error = errno;
  char description[128];
  // SAFETY: The pointer and exact capacity refer to the same local array.
  const char* reason =
      UNSAFE_BUFFERS(strerror_r(error, description, sizeof(description)));
  if (message && *message) {
    gtos::WriteDiagnostic(message);
    gtos::WriteDiagnostic(": ");
  }
  gtos::WriteDiagnostic(reason);
  gtos::WriteDiagnostic("\n");
  errno = error;
}

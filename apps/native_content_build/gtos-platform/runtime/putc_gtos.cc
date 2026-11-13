// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "base/compiler_specific.h"

extern "C" int putc(int value, FILE* stream) noexcept {
  const auto byte = static_cast<unsigned char>(value);
  // SAFETY: The source object is exactly one byte.
  return UNSAFE_BUFFERS(fwrite(&byte, 1, 1, stream)) == 1 ? byte : EOF;
}

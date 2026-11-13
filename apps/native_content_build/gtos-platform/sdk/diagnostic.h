// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_CONTENT_DIAGNOSTIC_H_
#define GTOS_CONTENT_DIAGNOSTIC_H_

#include <process/abi.h>

#include <string_view>

namespace gtos {

inline bool WriteDiagnostic(std::string_view text) {
  while (!text.empty()) {
    const auto part = text.substr(0, GTOS_NATIVE_WRITE_LIMIT);
    unsigned result = GTOS_SYS_WRITE;
    asm volatile("int $0x80"
                 : "+a"(result)
                 : "b"(part.data()), "c"(part.size())
                 : "memory", "cc");
    if (result != part.size()) {
      return false;
    }
    text.remove_prefix(part.size());
  }
  return true;
}

}  // namespace gtos

#endif  // GTOS_CONTENT_DIAGNOSTIC_H_

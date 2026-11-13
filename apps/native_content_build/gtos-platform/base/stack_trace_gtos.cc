// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/stack_trace.h"

#include <stdio.h>

#include <array>
#include <ostream>

#include "base/compiler_specific.h"

namespace base::debug {

NOINLINE size_t CollectStackTrace(span<const void*> trace) {
  // Native GTOS has no unwind tables. The immediate return address does not
  // require walking a frame chain through unverified memory.
  if (trace.empty()) {
    return 0;
  }
  const void* caller =
      __builtin_extract_return_addr(__builtin_return_address(0));
  if (!caller) {
    return 0;
  }
  trace.front() = caller;
  return 1;
}

void StackTrace::PrintMessageWithPrefix(cstring_view prefix,
                                        cstring_view message) {
  if (stderr) {
    // SAFETY: cstring_view carries the lengths of both live strings.
    UNSAFE_BUFFERS(fwrite(prefix.data(), 1, prefix.size(), stderr));
    UNSAFE_BUFFERS(fwrite(message.data(), 1, message.size(), stderr));
  }
}

void StackTrace::PrintWithPrefixImpl(cstring_view prefix) const {
  for (const void* address : addresses()) {
    std::array<char, 32> line;
    const int length = snprintf(line.data(), line.size(), "%p\n", address);
    if (length > 0 && static_cast<size_t>(length) < line.size()) {
      // SAFETY: snprintf wrote a terminated string into line.
      PrintMessageWithPrefix(prefix, UNSAFE_BUFFERS(cstring_view(line.data())));
    }
  }
}

void StackTrace::OutputToStreamWithPrefixImpl(std::ostream* stream,
                                             cstring_view prefix) const {
  for (const void* address : addresses()) {
    std::array<char, 32> line;
    const int length = snprintf(line.data(), line.size(), "%p\n", address);
    if (length > 0 && static_cast<size_t>(length) < line.size()) {
      *stream << prefix;
      stream->write(line.data(), length);
    }
  }
}

}  // namespace base::debug

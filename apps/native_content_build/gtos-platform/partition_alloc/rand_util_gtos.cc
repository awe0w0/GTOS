// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition_alloc/partition_alloc_base/rand_util.h"

#include "partition_alloc/partition_alloc_base/check.h"
#include "tools/gtos_native_sdk/native-services.h"

namespace partition_alloc::internal::base {
namespace {

bool HasHardwareEntropy() {
  uint32_t eax, ebx, ecx, edx;
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
               : "a"(0u), "c"(0u));
  if (eax < 1) {
    return false;
  }
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
               : "a"(1u), "c"(0u));
  return (ecx & (1u << 30)) != 0;
}

uint32_t RandomWord() {
  for (unsigned attempt = 0; attempt != 10; ++attempt) {
    uint32_t value;
    uint8_t ready;
    asm volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ready) : : "cc");
    if (ready) {
      return value;
    }
  }
  gtos::native::UnsupportedService();
}

}  // namespace

void RandBytes(void* output, size_t length) {
  if (!length) {
    return;
  }
  // This is the same CPUID and bounded RDRAND policy used by native V8 and
  // Rust std. There is no deterministic source when hardware entropy fails.
  PA_BASE_CHECK(HasHardwareEntropy());
  const auto info = gtos::native::CurrentProcessInfo();
  const auto address = reinterpret_cast<uintptr_t>(output);
  PA_BASE_CHECK(address >= info.user_begin && address < info.user_end &&
                length <= info.user_end - address);
  auto* bytes = static_cast<uint8_t*>(output);
  while (length) {
    uint32_t word = RandomWord();
    for (unsigned i = 0; i < sizeof(word) && length; ++i) {
      *PA_UNSAFE_TODO(bytes++) = static_cast<uint8_t>(word);
      word >>= 8;
      --length;
    }
  }
}

}  // namespace partition_alloc::internal::base

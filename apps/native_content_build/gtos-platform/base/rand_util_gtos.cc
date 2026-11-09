// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/rand_util.h"

#include <atomic>

#include "base/feature_list.h"
#include "partition_alloc/partition_alloc_base/rand_util.h"
#include "third_party/boringssl/src/include/openssl/rand.h"

namespace base {
namespace {

std::atomic<bool> g_use_boringssl;

BASE_FEATURE(kUseBoringSSLForRandBytes, FEATURE_ENABLED_BY_DEFAULT);

}  // namespace

namespace internal {

void ConfigureBoringSSLBackedRandBytesFieldTrial() {
  g_use_boringssl.store(FeatureList::IsEnabled(kUseBoringSSLForRandBytes),
                        std::memory_order_relaxed);
}

double RandDoubleAvoidAllocation() {
  uint64_t number;
  partition_alloc::internal::base::RandBytes(&number, sizeof(number));
  return (number >> 11) * 0x1.0p-53;
}

}  // namespace internal

void RandBytes(span<uint8_t> output) {
  if (output.empty())
    return;
  if (g_use_boringssl.load(std::memory_order_relaxed)) {
    // RAND_bytes terminates on failure, as does the native hardware provider.
    (void)RAND_bytes(output.data(), output.size());
    return;
  }
  partition_alloc::internal::base::RandBytes(output.data(), output.size());
}

}  // namespace base

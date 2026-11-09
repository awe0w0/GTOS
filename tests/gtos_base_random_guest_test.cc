// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/rand_util.h"

#include <array>

#include "third_party/boringssl/src/include/openssl/rand.h"

int TestBaseRandom() {
    std::array<uint8_t, 35> first{};
    first.front() = 0xa5;
    first.back() = 0x5a;
    auto second = first;
    base::RandBytes(base::span(first).subspan<1, 0>());
    if (first != second) return 15;

    base::RandBytes(base::span(first).subspan<1, 33>());
    base::RandBytes(base::span(second).subspan<1, 33>());
    if (first == second || first.front() != 0xa5 || second.front() != 0xa5 ||
        first.back() != 0x5a || second.back() != 0x5a) return 16;

    for (unsigned i = 0; i < 16; ++i) {
        const double value = base::internal::RandDoubleAvoidAllocation();
        if (!(value >= 0 && value < 1)) return 17;
    }
    if (RAND_bytes(first.data() + 1, 33) != 1 ||
        RAND_bytes(second.data() + 1, 33) != 1 || first == second ||
        first.front() != 0xa5 || second.front() != 0xa5 ||
        first.back() != 0x5a || second.back() != 0x5a) return 18;
    return 0;
}

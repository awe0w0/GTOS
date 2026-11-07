// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

extern "C" double tanh(double value) noexcept {
  if (value == 0.0 || value != value) {
    return value;
  }
  const bool negative = value < 0.0;
  const double magnitude = negative ? -value : value;
  if (magnitude <= 0.125) {
    // The odd Taylor series avoids subtracting nearly equal exponentials.
    const double square = value * value;
    const double series =
        -1.0 / 3.0 +
        square * (2.0 / 15.0 +
                  square * (-17.0 / 315.0 +
                            square * (62.0 / 2835.0 +
                                      square * (-1382.0 / 155925.0 +
                                                square * (21844.0 / 6081075.0)))));
    return value * (1.0 + square * series);
  }

  // Use the runtime's exp2 to keep the exponential in [0, 1].
  constexpr double kTwiceLog2E = 0x1.71547652b82fep+1;
  const double exponential =
      magnitude < 20.0 ? exp2(-magnitude * kTwiceLog2E) : 0.0;
  const double result = (1.0 - exponential) / (1.0 + exponential);
  return negative ? -result : result;
}

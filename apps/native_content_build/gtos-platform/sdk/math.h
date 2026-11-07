// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_SDK_MATH_H_
#define GTOS_SDK_MATH_H_

#include_next <math.h>

#ifdef __cplusplus
extern "C" double tanh(double value) noexcept;
#else
double tanh(double value);
#endif

#endif  // GTOS_SDK_MATH_H_

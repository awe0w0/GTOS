// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8/src/base/ieee754.h"

extern "C" double cosh(double value) {
  return v8::base::ieee754::cosh(value);
}

extern "C" double sinh(double value) {
  return v8::base::ieee754::sinh(value);
}

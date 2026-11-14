// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/config/gpu_info_collector.h"

namespace gpu {

bool CollectBasicGraphicsInfo(GPUInfo*) {
  // The native Ozone backend has a CPU surface and no hardware GPU query.
  return false;
}

bool CollectContextGraphicsInfo(GPUInfo*) {
  return false;
}

}  // namespace gpu

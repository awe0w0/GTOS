// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_GTOS_PRIVATE_MAPPING_H_
#define BASE_MEMORY_GTOS_PRIVATE_MAPPING_H_

#include <stddef.h>

#include "base/base_export.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_GTOS)
namespace base::internal {

// Returns page-aligned, zeroed private storage, or nullptr on VM failure.
// GTOS commits these pages eagerly. One extra, read-only page retains the
// native reservation handle; no heap allocation or global registry is used.
BASE_EXPORT void* GtosAllocatePrivateMapping(size_t size);

// Releases the entire reservation, including its handle page. The exact
// original size and address are required; misuse terminates the process group.
BASE_EXPORT void GtosReleasePrivateMapping(void* memory, size_t size);

}  // namespace base::internal
#endif

#endif  // BASE_MEMORY_GTOS_PRIVATE_MAPPING_H_

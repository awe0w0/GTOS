// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FILES_GTOS_FILE_MAPPING_H_
#define BASE_FILES_GTOS_FILE_MAPPING_H_

#include <stddef.h>

#include "base/base_export.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_GTOS)
namespace base::internal {

// Flushes |length| bytes from the start of an actual shared file mapping.
// The native provider must validate the mapping and range and honor |sync|.
// Returns false if validation or the backing-store operation fails.
BASE_EXPORT bool GtosFlushFileMapping(const void* memory,
                                      size_t length,
                                      bool sync);

}  // namespace base::internal
#endif  // BUILDFLAG(IS_GTOS)

#endif  // BASE_FILES_GTOS_FILE_MAPPING_H_

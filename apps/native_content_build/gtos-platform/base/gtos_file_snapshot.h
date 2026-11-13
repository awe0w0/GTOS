// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FILES_GTOS_FILE_SNAPSHOT_H_
#define BASE_FILES_GTOS_FILE_SNAPSHOT_H_

#include <stddef.h>
#include <stdint.h>

#include "base/containers/span.h"

namespace base {
class File;

// Private mapped copy of a known file extent. READ_ONLY is applied to
// each chunk before returning. Failure releases all reserved pages.
span<uint8_t> MapGtosFileSnapshot(File& file, int64_t offset, size_t size,
                                 bool readonly);
void UnmapGtosFileSnapshot(const void* address, size_t size);
}  // namespace base

#endif  // BASE_FILES_GTOS_FILE_SNAPSHOT_H_

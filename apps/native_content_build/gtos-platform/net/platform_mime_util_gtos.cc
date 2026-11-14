// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/platform_mime_util.h"

namespace net {

// Chromium's MIME tables cover GTOS. There is no system MIME registry.
bool PlatformMimeUtil::GetPlatformMimeTypeFromExtension(
    const base::FilePath::StringType& extension,
    std::string* result) const {
  return false;
}

bool PlatformMimeUtil::GetPlatformPreferredExtensionForMimeType(
    std::string_view mime_type,
    base::FilePath::StringType* extension) const {
  return false;
}

void PlatformMimeUtil::GetPlatformExtensionsForMimeType(
    std::string_view mime_type,
    std::unordered_set<base::FilePath::StringType>* extensions) const {}

}  // namespace net

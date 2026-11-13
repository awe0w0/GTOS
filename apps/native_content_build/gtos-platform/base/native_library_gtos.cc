// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/native_library.h"

namespace base {

std::string NativeLibraryLoadError::ToString() const {
  return message;
}

NativeLibrary LoadNativeLibrary(const FilePath&,
                                NativeLibraryLoadError* error) {
  if (error) {
    error->message = "GTOS does not load shared libraries";
  }
  return nullptr;
}

NativeLibrary LoadNativeLibraryWithOptions(const FilePath& library_path,
                                           const NativeLibraryOptions&,
                                           NativeLibraryLoadError* error) {
  return LoadNativeLibrary(library_path, error);
}

void UnloadNativeLibrary(NativeLibrary) {}

void* GetFunctionPointerFromNativeLibrary(NativeLibrary, const char*) {
  return nullptr;
}

std::string GetNativeLibraryName(std::string_view name) {
  return std::string("lib").append(name).append(".so");
}

std::string GetLoadableModuleName(std::string_view name) {
  return GetNativeLibraryName(name);
}

}  // namespace base

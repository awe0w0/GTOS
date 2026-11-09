// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_NATIVE_SDK_FILE_OUTPUT_HPP_
#define GTOS_NATIVE_SDK_FILE_OUTPUT_HPP_

#include <cstdio>
#include <ostream>
#include <streambuf>

namespace gtos::native {

// The GTOS libc++ profile omits filesystem streams. Keep ostream formatting
// and use the native FILE provider for writes, flush and close.
class FileOutput : private std::streambuf, public std::ostream {
 public:
  explicit FileOutput(const char* path);
  FileOutput(const char* path, std::ios_base::openmode mode);
  ~FileOutput() override;
  bool is_open() const { return file_ != nullptr; }
  void close();

 private:
  std::streamsize xsputn(const char* text, std::streamsize size) override;
  std::streambuf::int_type overflow(std::streambuf::int_type value) override;
  int sync() override;

  FILE* file_;
};

}  // namespace gtos::native

#endif  // GTOS_NATIVE_SDK_FILE_OUTPUT_HPP_

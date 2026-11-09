// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_NATIVE_SDK_FILE_INPUT_HPP_
#define GTOS_NATIVE_SDK_FILE_INPUT_HPP_

#include <array>
#include <cstdio>
#include <istream>
#include <streambuf>

namespace gtos::native {

// A sequential input stream backed by the native FILE service.
class FileInput : private std::streambuf, public std::istream {
 public:
  explicit FileInput(const char* path);
  ~FileInput() override;
  bool is_open() const { return file_ != nullptr; }

 private:
  std::streambuf::int_type underflow() override;

  FILE* file_;
  std::array<char, 4096> buffer_;
};

}  // namespace gtos::native

#endif  // GTOS_NATIVE_SDK_FILE_INPUT_HPP_

// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file-input.hpp"

#include <iterator>

namespace gtos::native {

FileInput::FileInput(const char* path)
    : std::istream(static_cast<std::streambuf*>(this)),
      file_(std::fopen(path, "rb")) {
  if (!file_)
    setstate(std::ios::failbit);
}

FileInput::~FileInput() {
  if (file_)
    std::fclose(file_);
}

std::streambuf::int_type FileInput::underflow() {
  using Traits = std::streambuf::traits_type;
  if (!file_)
    return Traits::eof();
  const size_t count = std::fread(buffer_.data(), 1, buffer_.size(), file_);
  if (std::ferror(file_)) {
    setstate(std::ios::badbit);
    return Traits::eof();
  }
  if (!count)
    return Traits::eof();
  char* data = buffer_.data();
  setg(data, data, std::next(data, count));
  return Traits::to_int_type(*data);
}

}  // namespace gtos::native

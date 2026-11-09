// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file-output.hpp"

namespace gtos::native {

FileOutput::FileOutput(const char* path)
    : FileOutput(path, std::ios_base::trunc) {}

FileOutput::FileOutput(const char* path, std::ios_base::openmode mode)
    : std::ostream(static_cast<std::streambuf*>(this)), file_(nullptr) {
  if (mode == std::ios_base::trunc || mode == std::ios_base::app) {
    file_ = std::fopen(path, mode == std::ios_base::app ? "ab" : "wb");
  }
  if (!file_)
    setstate(std::ios::failbit);
}

FileOutput::~FileOutput() {
  if (file_)
    std::fclose(file_);
}

void FileOutput::close() {
  FILE* closing = file_;
  file_ = nullptr;
  if (!closing || std::fclose(closing) != 0)
    setstate(std::ios::failbit);
}

std::streamsize FileOutput::xsputn(const char* text, std::streamsize size) {
  if (!file_ || size <= 0)
    return 0;
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
  return static_cast<std::streamsize>(
      std::fwrite(text, 1, static_cast<size_t>(size), file_));
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
}

std::streambuf::int_type FileOutput::overflow(std::streambuf::int_type value) {
  using Traits = std::streambuf::traits_type;
  if (!file_)
    return Traits::eof();
  if (Traits::eq_int_type(value, Traits::eof()))
    return Traits::not_eof(value);
  return std::fputc(static_cast<unsigned char>(Traits::to_char_type(value)),
                    file_) == EOF
             ? Traits::eof()
             : value;
}

int FileOutput::sync() {
  return file_ ? std::fflush(file_) : -1;
}

}  // namespace gtos::native

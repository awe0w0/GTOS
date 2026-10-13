//===-- Pointer specifier converter for scanf -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_SCANF_CORE_PTR_CONVERTER_H
#define LLVM_LIBC_SRC_STDIO_SCANF_CORE_PTR_CONVERTER_H

#include "src/__support/ctype_utils.h"
#include "src/__support/macros/config.h"
#include "src/stdio/scanf_core/core_structs.h"
#include "src/stdio/scanf_core/int_converter.h"
#include "src/stdio/scanf_core/reader.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
namespace scanf_core {

template <typename T>
int convert_pointer(Reader<T> *reader, const FormatSection &to_conv) {
  static const char nullptr_string[] = "(nullptr)";

  // Check if it's exactly the nullptr string, if so then it's a nullptr.
  char cur_char = reader->getc();
  size_t i = 0;
  size_t width = to_conv.max_width > 0 ? to_conv.max_width : sizeof(nullptr_string) - 1;
  for (; i < (sizeof(nullptr_string) - 1) && i < width &&
         internal::tolower(cur_char) == nullptr_string[i];
       ++i) {
    cur_char = reader->getc();
  }
  if (i == (sizeof(nullptr_string) - 1)) {
    reader->ungetc(cur_char);
    if ((to_conv.flags & NO_WRITE) == 0)
      *reinterpret_cast<void **>(to_conv.output_ptr) = nullptr;
    return READ_OK;
  } else if (i > 0) {
    reader->ungetc(cur_char);
    return MATCHING_FAILURE;
  }

  reader->ungetc(cur_char);

  // Else treat it as a hex int
  return convert_int(reader, to_conv);
}

} // namespace scanf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_SCANF_CORE_PTR_CONVERTER_H

//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
// Only the pinned throw helper consumed by chrono; no-exception branch retained.
#include <__config>
#include <__system_error/throw_system_error.h>
#include <__verbose_abort>

#if !defined(__GTOS__) || GTOS_ISOLATE_COMPILE_DIAGNOSTIC != 1 || _LIBCPP_HAS_EXCEPTIONS
#error "GTOS system-error termination requires the explicit no-exception profile"
#endif

_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
void __throw_system_error(int ev, const char* what_arg) {
#if _LIBCPP_HAS_EXCEPTIONS
  std::__throw_system_error(error_code(ev, generic_category()), what_arg);
#else
  // The above could also handle the no-exception case, but for size, avoid referencing system_category() unnecessarily.
  _LIBCPP_VERBOSE_ABORT(
      "system_error was thrown in -fno-exceptions mode with error %i and message \"%s\"", ev, what_arg);
#endif
}
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

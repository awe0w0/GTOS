//===-- GTOS application properties ------------------------------*- C++ -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef LLVM_LIBC_CONFIG_GTOS_APP_H
#define LLVM_LIBC_CONFIG_GTOS_APP_H
#include <stdint.h>
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
struct AppProperties {
  uintptr_t *env_ptr;
};
// Constant zero initialization; the native entry publishes startup properties
// before invoking constructors or creating any worker thread.
LIBC_INLINE_VAR AppProperties app;
} // namespace LIBC_NAMESPACE_DECL
#endif

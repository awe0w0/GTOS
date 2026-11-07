// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_SDK_SETJMP_H_
#define GTOS_SDK_SETJMP_H_

#if !defined(__GTOS__) || !defined(__i386__)
#error "This setjmp ABI is for GTOS i386"
#endif

#include <stdint.h>

// A saved context is valid within the same thread while its frame is live.
typedef struct {
  uint32_t __registers[6];
} jmp_buf[1];

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((returns_twice)) int setjmp(jmp_buf context);
__attribute__((noreturn)) void longjmp(jmp_buf context, int value);

#ifdef __cplusplus
}
#endif

#endif  // GTOS_SDK_SETJMP_H_

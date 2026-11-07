// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_SDK_ONCE_H_
#define GTOS_SDK_ONCE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The caller supplies a zero-initialized flag with static storage duration.
void gtos_call_once(uint32_t* flag, void (*function)(void));

#ifdef __cplusplus
}
#endif

#endif  // GTOS_SDK_ONCE_H_

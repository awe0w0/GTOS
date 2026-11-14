// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTOS_SDK_SCHED_H_
#define GTOS_SDK_SCHED_H_

#include <errno.h>
#include <process/abi.h>

static inline int sched_yield(void) {
  unsigned operation = GTOS_SYS_YIELD;
  __asm__ volatile("int $0x80" : "+a"(operation)
                   : "b"(0U), "c"(0U) : "memory", "cc");
  if (operation != 0) {
    errno = EIO;
    return -1;
  }
  return 0;
}

#endif  // GTOS_SDK_SCHED_H_

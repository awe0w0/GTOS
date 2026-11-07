// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GTOS_NATIVE_SDK_NATIVE_THREAD_H_
#define TOOLS_GTOS_NATIVE_SDK_NATIVE_THREAD_H_

#include <stddef.h>
#include <stdint.h>

namespace gtos::native {

using ThreadRoutine = void* (*)(void*);

inline constexpr size_t kDefaultThreadStackBytes = 64 * 1024;

// Returns a positive error on failure and leaves the caller's output unchanged.
// A zero stack size selects the default. Stacks are bounded by the native VM
// and shared-task profile and have inaccessible pages at both ends.
int StartThread(size_t stack_bytes, ThreadRoutine routine, void* argument,
                uint32_t* thread_id);
int JoinThread(uint32_t thread_id, void** result = nullptr);

// Detached workers retain their guarded stacks until the next collection.
// Collection never waits for a running worker. StartThread also collects.
int DetachThread(uint32_t thread_id);
unsigned CollectDetachedThreads();

}  // namespace gtos::native

#endif  // TOOLS_GTOS_NATIVE_SDK_NATIVE_THREAD_H_

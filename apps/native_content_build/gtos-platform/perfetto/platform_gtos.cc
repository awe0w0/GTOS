// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perfetto/tracing/platform.h"

#include "perfetto/ext/base/thread_task_runner.h"

namespace perfetto {
namespace {

class PlatformGtos final : public Platform {
 public:
  ThreadLocalObject* GetOrCreateThreadLocalObject() override {
    struct ThreadState {
      // TracingTLS may reenter this platform while its destructor runs.
      ~ThreadState() { delete object; }
      ThreadLocalObject* object = nullptr;
    };
    // Each native thread must destroy its own trace state when it exits.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
    static thread_local ThreadState state;
#pragma clang diagnostic pop
    if (!state.object) {
      state.object = ThreadLocalObject::CreateInstance().release();
    }
    return state.object;
  }

  std::unique_ptr<base::TaskRunner> CreateTaskRunner(
      const CreateTaskRunnerArgs& args) override {
    return std::make_unique<base::ThreadTaskRunner>(
        base::ThreadTaskRunner::CreateAndStart(args.name_for_debugging));
  }

  std::string GetCurrentProcessName() override { return "unknown_producer"; }
};

}  // namespace

Platform* Platform::GetDefaultPlatform() {
  static auto* const platform = new PlatformGtos;
  return platform;
}

}  // namespace perfetto

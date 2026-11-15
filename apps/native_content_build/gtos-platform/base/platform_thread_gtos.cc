// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread.h"

#include <__external_threading>

#include <chrono>
#include <memory>

#include "base/check_op.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/threading/platform_thread_internal.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_id_name_manager.h"
#include "base/threading/thread_restrictions.h"
#include "partition_alloc/buildflags.h"
#include "tools/gtos_native_sdk/native-services.h"
#include "tools/gtos_native_sdk/native-thread.h"

#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
#include "partition_alloc/stack/stack.h"  // nogncheck
#endif

namespace base {
namespace {

// Blink intrinsic layout can exhaust the SDK's 64 KiB worker default.
// Leave space for the two guard pages within GTOS's 64-page stack region.
constexpr size_t kChromiumThreadStackBytes = 240 * 1024;

struct ThreadParams {
  raw_ptr<PlatformThreadBase::Delegate> delegate;
  ThreadType type;
  bool joinable;
};

void* RunDelegate(void* argument) {
  std::unique_ptr<ThreadParams> params(static_cast<ThreadParams*>(argument));
  auto* delegate = params->delegate.get();
  if (!params->joinable) {
    DisallowSingleton();
  }
#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  partition_alloc::internal::StackTopRegistry::Get().NotifyThreadCreated();
#endif
  PlatformThreadBase::SetDefaultThreadType(params->type);
  params.reset();

  auto* names = ThreadIdNameManager::GetInstance();
  const auto id = PlatformThreadBase::CurrentId();
  const auto handle = PlatformThreadBase::CurrentHandle().platform_handle();
  names->RegisterThread(handle, id);
  delegate->ThreadMain();
  names->RemoveName(handle, id);
#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  partition_alloc::internal::StackTopRegistry::Get().NotifyThreadDestroyed();
#endif
  return nullptr;
}

bool StartDelegate(size_t stack_size, PlatformThreadBase::Delegate* delegate,
                   ThreadType type, PlatformThreadHandle* handle) {
  CHECK(delegate);
  if (!stack_size) {
    stack_size = kChromiumThreadStackBytes;
  }
  auto params = std::make_unique<ThreadParams>(delegate, type, handle != nullptr);
  uint32_t id = 0;
  const int error =
      gtos::native::StartThread(stack_size, RunDelegate, params.get(), &id);
  if (error != 0) {
    LOG(ERROR) << "Native thread creation returned " << error
               << " for requested stack size " << stack_size;
    if (handle) {
      *handle = PlatformThreadHandle();
    }
    return false;
  }
  params.release();
  if (handle) {
    *handle = PlatformThreadHandle(id);
  } else {
    CHECK_EQ(gtos::native::DetachThread(id), 0);
  }
  return true;
}

}  // namespace

PlatformThreadId PlatformThreadBase::CurrentId() {
  return PlatformThreadId(gtos::native::CurrentProcessInfo().thread_id);
}

PlatformThreadRef PlatformThreadBase::CurrentRef() {
  return PlatformThreadRef(CurrentId().raw());
}

PlatformThreadHandle PlatformThreadBase::CurrentHandle() {
  return PlatformThreadHandle(CurrentId().raw());
}

void PlatformThreadBase::YieldCurrentThread() {
  std::__libcpp_thread_yield();
}

void PlatformThreadBase::Sleep(TimeDelta duration) {
  std::__libcpp_thread_sleep_for(std::chrono::nanoseconds(duration.InNanoseconds()));
}

void PlatformThreadBase::SetName(const std::string& name) {
  SetNameCommon(name);
}

const char* PlatformThreadBase::GetName() {
  return ThreadIdNameManager::GetInstance()->GetName(CurrentId());
}

bool PlatformThreadBase::CreateWithType(size_t stack_size,
                                        Delegate* delegate,
                                        PlatformThreadHandle* thread_handle,
                                        ThreadType thread_type,
                                        MessagePumpType) {
  CHECK(thread_handle);
  return StartDelegate(stack_size, delegate, thread_type, thread_handle);
}

bool PlatformThreadBase::CreateNonJoinable(size_t stack_size,
                                           Delegate* delegate) {
  return CreateNonJoinableWithType(stack_size, delegate, ThreadType::kDefault);
}

bool PlatformThreadBase::CreateNonJoinableWithType(size_t stack_size,
                                                   Delegate* delegate,
                                                   ThreadType thread_type,
                                                   MessagePumpType) {
  return StartDelegate(stack_size, delegate, thread_type, nullptr);
}

void PlatformThreadBase::Join(PlatformThreadHandle thread_handle) {
  internal::ScopedBlockingCallWithBaseSyncPrimitives blocking(
      FROM_HERE, BlockingType::MAY_BLOCK);
  CHECK_EQ(gtos::native::JoinThread(thread_handle.platform_handle()), 0);
}

void PlatformThreadBase::Detach(PlatformThreadHandle thread_handle) {
  CHECK_EQ(gtos::native::DetachThread(thread_handle.platform_handle()), 0);
}

bool PlatformThreadBase::CanChangeThreadType(ThreadType from, ThreadType to) {
  return from == to;
}

ThreadType PlatformThreadBase::GetCurrentEffectiveThreadTypeForTest() {
  return ThreadType::kDefault;
}

size_t PlatformThreadBase::GetDefaultThreadStackSize() {
  return kChromiumThreadStackBytes;
}

internal::PlatformPriorityOverride internal::SetThreadTypeOverride(
    PlatformThreadHandle, ThreadType) {
  // Native tasks currently share one scheduling class.
  return false;
}

void internal::RemoveThreadTypeOverride(
    PlatformThreadHandle, const PlatformPriorityOverride& priority_override,
    ThreadType) {
  DCHECK(!priority_override);
}

void internal::SetCurrentThreadTypeImpl(ThreadType, MessagePumpType) {
  // The Chromium bookkeeping keeps the logical type. The native scheduler
  // uses the same scheduling policy for every admitted worker.
}

}  // namespace base

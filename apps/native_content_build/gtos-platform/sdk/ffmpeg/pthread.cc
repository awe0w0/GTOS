// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pthread.h"

#include <errno.h>

#include "tools/gtos_native_sdk/native-thread.h"
#include "tools/gtos_native_sdk/native-services.h"

static_assert(sizeof(pthread_mutex_t) == 8 && alignof(pthread_mutex_t) == 4);
static_assert(sizeof(pthread_cond_t) == 8 && alignof(pthread_cond_t) == 4);

namespace {

bool SupportedAttributes(const pthread_attr_t* attr) {
  return attr && attr->__detachstate == 0 && attr->__inheritsched == 0 &&
         attr->__schedpolicy == 0 && attr->__schedparam.sched_priority == 0 &&
         attr->__stack == nullptr && attr->__guardsize == 4096 &&
         attr->__stacksize >= PTHREAD_STACK_MIN;
}

}  // namespace

int pthread_attr_init(pthread_attr_t* attr) {
  if (!attr)
    return EINVAL;
  *attr = {};
  attr->__stacksize = gtos::native::kDefaultThreadStackBytes;
  attr->__guardsize = 4096;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
  if (!SupportedAttributes(attr))
    return EINVAL;
  *attr = {};
  attr->__detachstate = -1;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t bytes) {
  if (!SupportedAttributes(attr) || bytes < PTHREAD_STACK_MIN)
    return EINVAL;
  attr->__stacksize = bytes;
  return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*routine)(void*), void* argument) {
  if (attr && !SupportedAttributes(attr))
    return EINVAL;
  // StartThread enforces its VM bound. A requested stack is never silently
  // replaced with the default when it cannot be allocated by the native ABI.
  return gtos::native::StartThread(attr ? attr->__stacksize : 0, routine,
                                  argument, thread);
}

int pthread_join(pthread_t thread, void** result) {
  return gtos::native::JoinThread(thread, result);
}

pthread_t pthread_self() {
  return gtos::native::CurrentProcessInfo().thread_id;
}

int pthread_equal(pthread_t left, pthread_t right) {
  return left == right;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
  if (!attr)
    return EINVAL;
  attr->type = 0;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
  if (!attr || (type != 0 && type != PTHREAD_MUTEX_ERRORCHECK))
    return EINVAL;
  attr->type = type;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
  if (!attr)
    return EINVAL;
  attr->type = -1;
  return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
  if (!mutex || (attr && attr->type != 0 && attr->type != PTHREAD_MUTEX_ERRORCHECK))
    return EINVAL;
  *mutex = {};
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
  return std::__libcpp_mutex_destroy(mutex);
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
  return std::__libcpp_mutex_lock(mutex);
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
  if (!mutex)
    return EINVAL;
  return std::__libcpp_mutex_trylock(mutex) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
  return std::__libcpp_mutex_unlock(mutex);
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
  if (!cond || attr)
    return EINVAL;
  *cond = {};
  return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond) {
  return std::__libcpp_condvar_destroy(cond);
}

int pthread_cond_signal(pthread_cond_t* cond) {
  return std::__libcpp_condvar_signal(cond);
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
  return std::__libcpp_condvar_broadcast(cond);
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
  return std::__libcpp_condvar_wait(cond, mutex);
}

int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                           const struct timespec* deadline) {
  if (!deadline)
    return EINVAL;
  timespec until = *deadline;
  return std::__libcpp_condvar_timedwait(cond, mutex, &until);
}

int pthread_once(pthread_once_t* once, void (*routine)(void)) {
  return std::__libcpp_execute_once(once, routine);
}

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GTOS_NATIVE_SDK_FFMPEG_PTHREAD_H_
#define TOOLS_GTOS_NATIVE_SDK_FFMPEG_PTHREAD_H_

#if !defined(__GTOS__) || !defined(__i386__)
#error This header requires the native GTOS IA32 ABI.
#endif

#include <stdint.h>
#include <time.h>
#include <llvm-libc-types/pthread_attr_t.h>

// The native mutex and condition variable layouts are shared with libc++.
#ifdef __cplusplus
#include <__external_threading>
typedef std::__libcpp_mutex_t pthread_mutex_t;
typedef std::__libcpp_condvar_t pthread_cond_t;
extern "C" {
#else
typedef struct { uint32_t owner, depth; } pthread_mutex_t;
typedef struct { uint32_t waiters, sequence; } pthread_cond_t;
#endif

typedef uint32_t pthread_t;
typedef uint32_t pthread_once_t;
typedef struct gtos_pthread_condattr pthread_condattr_t;
typedef struct { int type; } pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER {0, 0}
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_STACK_MIN 8192

int pthread_attr_init(pthread_attr_t*);
int pthread_attr_destroy(pthread_attr_t*);
int pthread_attr_setstacksize(pthread_attr_t*, size_t);
int pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
int pthread_join(pthread_t, void**);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
int pthread_mutexattr_init(pthread_mutexattr_t*);
int pthread_mutexattr_settype(pthread_mutexattr_t*, int);
int pthread_mutexattr_destroy(pthread_mutexattr_t*);
int pthread_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*);
int pthread_mutex_destroy(pthread_mutex_t*);
int pthread_mutex_lock(pthread_mutex_t*);
int pthread_mutex_trylock(pthread_mutex_t*);
int pthread_mutex_unlock(pthread_mutex_t*);
int pthread_cond_init(pthread_cond_t*, const pthread_condattr_t*);
int pthread_cond_destroy(pthread_cond_t*);
int pthread_cond_signal(pthread_cond_t*);
int pthread_cond_broadcast(pthread_cond_t*);
int pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*);
int pthread_cond_timedwait(pthread_cond_t*, pthread_mutex_t*,
                           const struct timespec*);
int pthread_once(pthread_once_t*, void (*)(void));

#ifdef __cplusplus
}
#endif
#endif  // TOOLS_GTOS_NATIVE_SDK_FFMPEG_PTHREAD_H_

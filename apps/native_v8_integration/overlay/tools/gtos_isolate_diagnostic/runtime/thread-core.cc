#include <__external_threading>
#include <errno.h>
#include <stdint.h>

#include "src/base/platform/gtos-clock-bridge.h"
#include "src/base/platform/gtos-native-services.h"

// GTOS ABI1 has one user task in a private address space. Every state-changing
// operation validates that capability; these locks do not synchronize shared
// address spaces or advertise native thread creation.
namespace {
uint32_t CurrentOwner() {
  return v8::base::gtos::CurrentProcessInfo().thread_id;
}

bool ValidState(uint32_t owner, uint32_t depth) {
  return (owner == 0) == (depth == 0);
}
}  // namespace

_LIBCPP_BEGIN_NAMESPACE_STD

int __libcpp_mutex_lock(__libcpp_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  const uint32_t owner = CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth) || mutex->depth > 1) return EINVAL;
  if (mutex->owner != 0) return EDEADLK;
  mutex->owner = owner;
  mutex->depth = 1;
  return 0;
}

bool __libcpp_mutex_trylock(__libcpp_mutex_t* mutex) {
  if (mutex == nullptr) return false;
  const uint32_t owner = CurrentOwner();
  if (mutex->owner != 0 || mutex->depth != 0) return false;
  mutex->owner = owner;
  mutex->depth = 1;
  return true;
}

int __libcpp_mutex_unlock(__libcpp_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  const uint32_t owner = CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth) || mutex->depth > 1) return EINVAL;
  if (mutex->owner != owner) return EPERM;
  mutex->owner = 0;
  mutex->depth = 0;
  return 0;
}

int __libcpp_mutex_destroy(__libcpp_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth) || mutex->depth > 1) return EINVAL;
  return mutex->owner == 0 ? 0 : EBUSY;
}

int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  CurrentOwner();
  mutex->owner = 0;
  mutex->depth = 0;
  return 0;
}

int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  const uint32_t owner = CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth)) return EINVAL;
  if (mutex->owner != 0 && mutex->owner != owner) return EPERM;
  if (mutex->depth == UINT32_MAX) return EAGAIN;
  mutex->owner = owner;
  ++mutex->depth;
  return 0;
}

bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return false;
  const uint32_t owner = CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth) ||
      (mutex->owner != 0 && mutex->owner != owner) ||
      mutex->depth == UINT32_MAX) {
    return false;
  }
  mutex->owner = owner;
  ++mutex->depth;
  return true;
}

int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  const uint32_t owner = CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth)) return EINVAL;
  if (mutex->owner != owner) return EPERM;
  --mutex->depth;
  if (mutex->depth == 0) mutex->owner = 0;
  return 0;
}

int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  CurrentOwner();
  if (!ValidState(mutex->owner, mutex->depth)) return EINVAL;
  return mutex->owner == 0 ? 0 : EBUSY;
}

int __libcpp_execute_once(__libcpp_exec_once_flag* flag, void (*function)()) {
  if (flag == nullptr || function == nullptr) return EINVAL;
  CurrentOwner();
  if (*flag == 2) return 0;
  if (*flag == 1) return EDEADLK;
  if (*flag != 0) return EINVAL;
  *flag = 1;
  function();
  *flag = 2;
  return 0;
}

bool __libcpp_thread_id_equal(__libcpp_thread_id a, __libcpp_thread_id b) {
  return a == b;
}

bool __libcpp_thread_id_less(__libcpp_thread_id a, __libcpp_thread_id b) {
  return a < b;
}

bool __libcpp_thread_isnull(const __libcpp_thread_t* thread) {
  return thread == nullptr || *thread == _LIBCPP_NULL_THREAD;
}

__libcpp_thread_id __libcpp_thread_get_current_id() {
  return CurrentOwner();
}

__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t* thread) {
  return thread == nullptr ? _LIBCPP_NULL_THREAD : *thread;
}

void __libcpp_thread_yield() {
  CurrentOwner();
  v8::base::gtos::Yield();
}

void __libcpp_thread_sleep_for(const chrono::nanoseconds& duration) {
  const int64_t nanoseconds = duration.count();
  if (nanoseconds <= 0) return;
  CurrentOwner();
  // Divide before rounding up: adding 999 to INT64_MAX would overflow.
  const uint64_t microseconds = static_cast<uint64_t>(nanoseconds / 1000) +
                               (nanoseconds % 1000 != 0);
  const int64_t begin = gtos_v8_clock_read_microseconds();
  for (;;) {
    v8::base::gtos::Yield();
    const int64_t now = gtos_v8_clock_read_microseconds();
    if (now < begin) v8::base::gtos::UnsupportedService();
    // Use elapsed time instead of an absolute addition that can overflow.
    if (static_cast<uint64_t>(now - begin) >= microseconds) return;
  }
}

_LIBCPP_END_NAMESPACE_STD

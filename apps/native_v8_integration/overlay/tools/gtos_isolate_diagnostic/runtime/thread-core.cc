#include <__external_threading>
#include <errno.h>
#include <stdint.h>

#include "src/base/platform/gtos-clock-bridge.h"
#include "src/base/platform/gtos-native-services.h"

// Locks retain their two-word ABI and synchronize the bounded native contexts
// sharing one private address space. The owner word is the atomic claim;
// only its owning thread can inspect or change the protected depth.
namespace {
uint32_t CurrentOwner() {
  return v8::base::gtos::CurrentProcessInfo().thread_id;
}

template <class Mutex>
int Acquire(Mutex* mutex, uint32_t owner, bool recursive) {
  uint32_t expected = 0;
  if (__atomic_compare_exchange_n(&mutex->owner, &expected, owner, false,
                                   __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
    if (__atomic_load_n(&mutex->depth, __ATOMIC_RELAXED) != 0) {
      __atomic_store_n(&mutex->owner, 0U, __ATOMIC_RELEASE);
      return EINVAL;
    }
    __atomic_store_n(&mutex->depth, 1U, __ATOMIC_RELAXED);
    return 0;
  }
  if (expected != owner) return expected > 0x7fffffffU ? EINVAL : EBUSY;
  const uint32_t depth = __atomic_load_n(&mutex->depth, __ATOMIC_RELAXED);
  if (depth == 0 || (!recursive && depth != 1)) return EINVAL;
  if (!recursive) return EDEADLK;
  if (depth == UINT32_MAX) return EAGAIN;
  __atomic_store_n(&mutex->depth, depth + 1, __ATOMIC_RELAXED);
  return 0;
}

template <class Mutex>
int Lock(Mutex* mutex, bool recursive) {
  if (mutex == nullptr) return EINVAL;
  const auto info = v8::base::gtos::CurrentProcessInfo();
  for (;;) {
    const int result = Acquire(mutex, info.thread_id, recursive);
    if (result != EBUSY) return result;
    if (info.maximum_parallel_threads == 1) return recursive ? EPERM : EDEADLK;
    v8::base::gtos::Yield();
  }
}

template <class Mutex>
int Unlock(Mutex* mutex, bool recursive) {
  if (mutex == nullptr) return EINVAL;
  const uint32_t owner = CurrentOwner();
  const uint32_t held = __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED);
  const uint32_t depth = __atomic_load_n(&mutex->depth, __ATOMIC_RELAXED);
  if (held == 0 && depth != 0) return EINVAL;
  if (held != owner) return EPERM;
  if (depth == 0 || (!recursive && depth != 1)) return EINVAL;
  __atomic_store_n(&mutex->depth, depth - 1, __ATOMIC_RELAXED);
  if (depth == 1) __atomic_store_n(&mutex->owner, 0U, __ATOMIC_RELEASE);
  return 0;
}

template <class Mutex>
int Destroy(Mutex* mutex, bool recursive) {
  if (mutex == nullptr) return EINVAL;
  CurrentOwner();
  const uint32_t owner = __atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE);
  const uint32_t depth = __atomic_load_n(&mutex->depth, __ATOMIC_RELAXED);
  if ((owner == 0) != (depth == 0) || (!recursive && depth > 1)) return EINVAL;
  return owner == 0 ? 0 : EBUSY;
}
}  // namespace

_LIBCPP_BEGIN_NAMESPACE_STD

int __libcpp_mutex_lock(__libcpp_mutex_t* mutex) {
  return Lock(mutex, false);
}

bool __libcpp_mutex_trylock(__libcpp_mutex_t* mutex) {
  if (mutex == nullptr) return false;
  const uint32_t owner = CurrentOwner();
  return Acquire(mutex, owner, false) == 0;
}

int __libcpp_mutex_unlock(__libcpp_mutex_t* mutex) {
  return Unlock(mutex, false);
}

int __libcpp_mutex_destroy(__libcpp_mutex_t* mutex) {
  return Destroy(mutex, false);
}

int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return EINVAL;
  CurrentOwner();
  __atomic_store_n(&mutex->owner, 0U, __ATOMIC_RELAXED);
  __atomic_store_n(&mutex->depth, 0U, __ATOMIC_RELAXED);
  return 0;
}

int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* mutex) {
  return Lock(mutex, true);
}

bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* mutex) {
  if (mutex == nullptr) return false;
  const uint32_t owner = CurrentOwner();
  return Acquire(mutex, owner, true) == 0;
}

int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* mutex) {
  return Unlock(mutex, true);
}

int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* mutex) {
  return Destroy(mutex, true);
}

int __libcpp_execute_once(__libcpp_exec_once_flag* flag, void (*function)()) {
  if (flag == nullptr || function == nullptr) return EINVAL;
  const auto info = v8::base::gtos::CurrentProcessInfo();
  const uint32_t pending = 0x80000000U | info.thread_id;
  for (;;) {
    const uint32_t state = __atomic_load_n(flag, __ATOMIC_ACQUIRE);
    if (state == 2) return 0;
    if (state == 1 || state == pending) return EDEADLK;
    if (state != 0 && (state <= 0x80000000U || info.maximum_parallel_threads == 1))
      return EINVAL;
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(flag, &expected, pending, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      break;
    v8::base::gtos::Yield();
  }
  function();
  __atomic_store_n(flag, 2U, __ATOMIC_RELEASE);
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

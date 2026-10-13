#include <__external_threading>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "src/base/platform/gtos-native-services.h"

// GTOS ABI1 permits one user task per private address space. These operations
// preserve lock ownership and real deadlines; they do not create peer threads.
// Waiting yields to the real scheduler. An untimed wait without notification
// stays pending until its owning process is cancelled.
namespace {
uint32_t CurrentOwner() {
  return v8::base::gtos::CurrentProcessInfo().thread_id;
}

bool Expired(const timespec& now, const timespec& deadline) {
  return now.tv_sec > deadline.tv_sec ||
         (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
}
}  // namespace

_LIBCPP_BEGIN_NAMESPACE_STD

namespace {
bool ValidCondition(__libcpp_condvar_t* condition) {
  return condition != nullptr && condition->waiters <= 1;
}

int Wait(__libcpp_condvar_t* condition, __libcpp_mutex_t* mutex,
         const __libcpp_timespec_t* deadline) {
  if (!ValidCondition(condition) || mutex == nullptr) return EINVAL;
  const uint32_t owner = CurrentOwner();
  if (condition->waiters != 0) return EBUSY;
  if (mutex->owner != owner || mutex->depth != 1) return EPERM;
  if (deadline != nullptr &&
      (deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000)) {
    return EINVAL;
  }
  // Snapshot the caller's deadline before releasing its mutex.
  const timespec until = deadline != nullptr ? *deadline : timespec{};
  const uint32_t sequence = condition->sequence;
  condition->waiters = 1;
  if (__libcpp_mutex_unlock(mutex) != 0) {
    v8::base::gtos::UnsupportedService();
  }
  int result = 0;
  for (;;) {
    if (condition->waiters != 1 || mutex->owner != 0 || mutex->depth != 0) {
      v8::base::gtos::UnsupportedService();
    }
    if (condition->sequence != sequence) break;
    if (deadline != nullptr) {
      timespec now = {};
      const int saved_errno = errno;
      const int status = clock_gettime(CLOCK_REALTIME, &now);
      const int error = errno;
      errno = saved_errno;
      if (status != 0) {
        result = error > 0 ? error : EIO;
        break;
      }
      if (Expired(now, until)) {
        result = ETIMEDOUT;
        break;
      }
    }
    v8::base::gtos::Yield();
  }
  condition->waiters = 0;
  if (__libcpp_mutex_lock(mutex) != 0) {
    v8::base::gtos::UnsupportedService();
  }
  return result;
}
}  // namespace

int __libcpp_condvar_signal(__libcpp_condvar_t* condition) {
  if (!ValidCondition(condition)) return EINVAL;
  CurrentOwner();
  ++condition->sequence;
  return 0;
}

int __libcpp_condvar_broadcast(__libcpp_condvar_t* condition) {
  // In this checked single-user-task profile there can be at most one waiter.
  return __libcpp_condvar_signal(condition);
}

int __libcpp_condvar_wait(__libcpp_condvar_t* condition,
                        __libcpp_mutex_t* mutex) {
  return Wait(condition, mutex, nullptr);
}

int __libcpp_condvar_timedwait(__libcpp_condvar_t* condition,
                             __libcpp_mutex_t* mutex,
                             __libcpp_timespec_t* deadline) {
  if (deadline == nullptr) return EINVAL;
  return Wait(condition, mutex, deadline);
}

int __libcpp_condvar_destroy(__libcpp_condvar_t* condition) {
  if (!ValidCondition(condition)) return EINVAL;
  CurrentOwner();
  return condition->waiters == 0 ? 0 : EBUSY;
}

_LIBCPP_END_NAMESPACE_STD

#include <__external_threading>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "src/base/platform/gtos-native-services.h"

// Up to four native contexts share one address space. Wait records stay local
// to that address space; notification selects one or all registered waiters.
// Real scheduler yields and clock deadlines preserve mutex ownership on return.
namespace {
bool Expired(const timespec& now, const timespec& deadline) {
  return now.tv_sec > deadline.tv_sec ||
         (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
}
}  // namespace

_LIBCPP_BEGIN_NAMESPACE_STD

namespace {
struct WaitRecord {
  __libcpp_condvar_t* condition;
  uint32_t owner;
  bool notified;
};
WaitRecord records[4];
uint32_t condition_lock;
class ConditionGuard {
 public:
  ConditionGuard() {
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(&condition_lock, &expected, 1U, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      v8::base::gtos::Yield();
      expected = 0;
    }
  }
  ~ConditionGuard() { __atomic_store_n(&condition_lock, 0U, __ATOMIC_RELEASE); }
  ConditionGuard(const ConditionGuard&) = delete;
  ConditionGuard& operator=(const ConditionGuard&) = delete;
};

bool ValidCondition(__libcpp_condvar_t* condition, uint32_t limit) {
  return condition != nullptr && __atomic_load_n(&condition->waiters, __ATOMIC_RELAXED) <= limit;
}

uint32_t Registered(__libcpp_condvar_t* condition) {
  uint32_t count = 0;
  for (const auto& record : records) if (record.condition == condition) ++count;
  return count;
}

int Wait(__libcpp_condvar_t* condition, __libcpp_mutex_t* mutex,
         const __libcpp_timespec_t* deadline) {
  if (condition == nullptr || mutex == nullptr) return EINVAL;
  const auto info = v8::base::gtos::CurrentProcessInfo();
  if (!ValidCondition(condition, info.maximum_parallel_threads)) return EINVAL;
  if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) != info.thread_id ||
      __atomic_load_n(&mutex->depth, __ATOMIC_RELAXED) != 1) return EPERM;
  if (deadline != nullptr &&
      (deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000)) {
    return EINVAL;
  }
  // Snapshot the caller's deadline before releasing its mutex.
  const timespec until = deadline != nullptr ? *deadline : timespec{};
  WaitRecord* waiting = nullptr;
  {
    ConditionGuard guard;
    const uint32_t count = __atomic_load_n(&condition->waiters, __ATOMIC_RELAXED);
    if (count > info.maximum_parallel_threads) return EINVAL;
    if (count >= info.maximum_parallel_threads || Registered(condition) != count) return EBUSY;
    for (auto& record : records) {
      if (record.owner == info.thread_id) return EDEADLK;
      if (record.condition == nullptr && waiting == nullptr) waiting = &record;
    }
    if (waiting == nullptr) return EBUSY;
    waiting->condition = condition;
    waiting->owner = info.thread_id;
    __atomic_store_n(&waiting->notified, false, __ATOMIC_RELAXED);
    __atomic_store_n(&condition->waiters, count + 1, __ATOMIC_RELAXED);
  }
  if (__libcpp_mutex_unlock(mutex) != 0) {
    v8::base::gtos::UnsupportedService();
  }
  int result = 0;
  for (;;) {
    if (__atomic_load_n(&waiting->notified, __ATOMIC_ACQUIRE)) break;
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
  {
    ConditionGuard guard;
    const uint32_t count = __atomic_load_n(&condition->waiters, __ATOMIC_RELAXED);
    if (waiting->condition != condition || waiting->owner != info.thread_id ||
        count == 0 || count > info.maximum_parallel_threads)
      v8::base::gtos::UnsupportedService();
    waiting->condition = nullptr;
    waiting->owner = 0;
    __atomic_store_n(&condition->waiters, count - 1, __ATOMIC_RELAXED);
  }
  if (__libcpp_mutex_lock(mutex) != 0) {
    v8::base::gtos::UnsupportedService();
  }
  return result;
}

int Notify(__libcpp_condvar_t* condition, bool all) {
  if (condition == nullptr) return EINVAL;
  const auto info = v8::base::gtos::CurrentProcessInfo();
  ConditionGuard guard;
  if (!ValidCondition(condition, info.maximum_parallel_threads)) return EINVAL;
  if (Registered(condition) != __atomic_load_n(&condition->waiters, __ATOMIC_RELAXED)) return EINVAL;
  __atomic_fetch_add(&condition->sequence, 1U, __ATOMIC_RELAXED);
  for (auto& record : records) {
    if (record.condition == condition && !__atomic_load_n(&record.notified, __ATOMIC_RELAXED)) {
      __atomic_store_n(&record.notified, true, __ATOMIC_RELEASE);
      if (!all) break;
    }
  }
  return 0;
}
}  // namespace

int __libcpp_condvar_signal(__libcpp_condvar_t* condition) {
  return Notify(condition, false);
}

int __libcpp_condvar_broadcast(__libcpp_condvar_t* condition) {
  return Notify(condition, true);
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
  if (condition == nullptr) return EINVAL;
  const auto info = v8::base::gtos::CurrentProcessInfo();
  ConditionGuard guard;
  if (!ValidCondition(condition, info.maximum_parallel_threads)) return EINVAL;
  return __atomic_load_n(&condition->waiters, __ATOMIC_RELAXED) == 0 ? 0 : EBUSY;
}

_LIBCPP_END_NAMESPACE_STD

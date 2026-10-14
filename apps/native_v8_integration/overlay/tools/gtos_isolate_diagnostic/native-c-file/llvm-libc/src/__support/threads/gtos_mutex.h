// GTOS platform mutex for the kernel-reported one- or four-context profile.
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_GTOS_MUTEX_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_GTOS_MUTEX_H
#include "src/__support/threads/mutex_common.h"
#include "src/base/platform/gtos-native-services.h"

namespace LIBC_NAMESPACE_DECL {
class Mutex {
  unsigned state = 0;
  unsigned owner = 0;
  bool unsupported;
public:
  constexpr Mutex(bool timed, bool recursive, bool robust, bool pshared,
                  bool = false)
      : unsupported(timed || recursive || robust || pshared) {}
  MutexError try_lock() {
    if (unsupported) __builtin_trap();
    unsigned id = v8::base::gtos::CurrentProcessInfo().thread_id;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&state, &expected, id, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return MutexError::BUSY;
    __atomic_store_n(&owner, id, __ATOMIC_RELAXED);
    return MutexError::NONE;
  }
  MutexError lock() {
    if (unsupported) __builtin_trap();
    const auto info = v8::base::gtos::CurrentProcessInfo();
    const unsigned id = info.thread_id;
    unsigned expected = 0;
    while (!__atomic_compare_exchange_n(&state, &expected, id, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      // FileLock ignores errors. Reject recursion instead of entering unlocked;
      // a different native thread can release the lock after a real yield.
      if (expected == id || info.maximum_parallel_threads == 1) __builtin_trap();
      v8::base::gtos::Yield();
      expected = 0;
    }
    __atomic_store_n(&owner, id, __ATOMIC_RELAXED);
    return MutexError::NONE;
  }
  MutexError unlock() {
    const unsigned id = v8::base::gtos::CurrentProcessInfo().thread_id;
    if (unsupported || __atomic_load_n(&state, __ATOMIC_RELAXED) != id ||
        __atomic_load_n(&owner, __ATOMIC_RELAXED) != id)
      __builtin_trap();
    __atomic_store_n(&owner, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
    return MutexError::NONE;
  }
  bool is_robust() const { return false; }
  MutexError reset() { return MutexError::BAD_LOCK_STATE; }
};
}
#endif

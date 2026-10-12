// GTOS platform mutex for the kernel-reported single user thread profile.
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
    unsigned id = v8::base::gtos::CurrentProcessInfo().process_id;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&state, &expected, 1, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return MutexError::BUSY;
    owner = id;
    return MutexError::NONE;
  }
  MutexError lock() {
    // There is no second user thread which could release a busy normal lock.
    // File::FileLock ignores errors, so never enter a critical section unlocked.
    if (try_lock() != MutexError::NONE) __builtin_trap();
    return MutexError::NONE;
  }
  MutexError unlock() {
    if (unsupported || !__atomic_load_n(&state, __ATOMIC_RELAXED) ||
        owner != v8::base::gtos::CurrentProcessInfo().process_id)
      __builtin_trap();
    owner = 0;
    __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
    return MutexError::NONE;
  }
  bool is_robust() const { return false; }
  MutexError reset() { return MutexError::BAD_LOCK_STATE; }
};
}
#endif

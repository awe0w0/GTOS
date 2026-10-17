#include "third_party/boringssl/src/crypto/internal.h"
#include "api.h"

#if !defined(OPENSSL_GTOS_THREADS)
#error "The diagnostic must call the real native BoringSSL thread backend"
#endif
static_assert(sizeof(unsigned) == 4 && sizeof(void *) == 4);
static_assert(sizeof(CryptoLockSnapshot) == 20);

BSSL_NAMESPACE_BEGIN
static_assert(NUM_OPENSSL_THREAD_LOCALS == 5);
namespace {
// Only diagnostics access protected storage, under the same gate or while
// quiescent. Lock operations themselves always call the production methods.
class ProbeLock : public StaticMutex {
 public:
  void Snapshot(CryptoLockSnapshot *output) {
    const unsigned tid = crypto_probe_current_tid();
    Enter(tid);
    *output = {lock_.next_ticket, lock_.serving_ticket, lock_.writer, 0, 0};
    for (const auto &reader : lock_.readers) {
      output->readers += reader.depth != 0;
      if (reader.owner == tid) {
        output->ownDepth = reader.depth;
      }
    }
    lock_.gate.store(0, std::memory_order_release);
  }

  void Seed(unsigned value) {
    Enter(crypto_probe_current_tid());
    BSSL_CHECK(lock_.writer == 0 && lock_.next_ticket == lock_.serving_ticket);
    for (const auto &reader : lock_.readers) {
      BSSL_CHECK(reader.owner == 0 && reader.depth == 0);
    }
    lock_.next_ticket = lock_.serving_ticket = value;
    lock_.gate.store(0, std::memory_order_release);
  }

 private:
  void Enter(unsigned tid) {
    uint32_t expected = 0;
    while (!lock_.gate.compare_exchange_strong(expected, tid,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
      BSSL_CHECK(expected != tid);
      crypto_probe_yield();
      expected = 0;
    }
  }
};

ProbeLock probe_lock;
CRYPTO_once_t probe_once{0}, nested_first{0}, nested_second{0}, recursive_once{0};
unsigned nested_count;
void NestedSecond() { nested_count++; }
void NestedFirst() {
  CRYPTO_once(&nested_second, NestedSecond);
  nested_count++;
}
void Recursive() { CRYPTO_once(&recursive_once, Recursive); }
}  // namespace

extern "C" void crypto_probe_lock(unsigned action) {
  switch (action) {
    case 0: probe_lock.LockRead(); return;
    case 1: probe_lock.UnlockRead(); return;
    case 2: probe_lock.LockWrite(); return;
    case 3: probe_lock.UnlockWrite(); return;
    default: BSSL_CHECK(false);
  }
}

extern "C" void crypto_probe_snapshot(CryptoLockSnapshot *output) {
  probe_lock.Snapshot(output);
}
extern "C" void crypto_probe_seed(unsigned value) { probe_lock.Seed(value); }
extern "C" void crypto_probe_once(void (*initialize)()) {
  CRYPTO_once(&probe_once, initialize);
}
extern "C" unsigned crypto_probe_nested_once() {
  CRYPTO_once(&nested_first, NestedFirst);
  CRYPTO_once(&nested_first, NestedFirst);
  return nested_count == 2;
}

extern "C" void crypto_probe_bad(unsigned mode) {
  if (mode == 8) {
    CRYPTO_once(&recursive_once, Recursive);
  } else if (mode == 11) {
    CRYPTO_get_thread_local(static_cast<thread_local_data_t>(5));
  } else if (mode == 12) {
    CRYPTO_once(&recursive_once, nullptr);
  } else {
    Mutex lock;
    switch (mode) {
      case 1: lock.UnlockRead(); break;
      case 2: lock.UnlockWrite(); break;
      case 3: lock.LockRead(); lock.LockWrite(); break;
      case 4: lock.LockWrite(); lock.LockWrite(); break;
      case 5: lock.LockRead(); return;  // Real destructor rejects held readers.
      case 6: lock.LockWrite(); return;  // Real destructor rejects held writer.
      case 7: lock.LockWrite(); lock.LockRead(); break;
      default: BSSL_CHECK(false);
    }
  }
}

extern "C" void *crypto_probe_get(unsigned index) {
  return CRYPTO_get_thread_local(static_cast<thread_local_data_t>(index));
}
extern "C" unsigned crypto_probe_set(unsigned index, void *value,
                                     void (*destroy)(void *)) {
  return CRYPTO_set_thread_local(static_cast<thread_local_data_t>(index), value,
                                 destroy);
}
BSSL_NAMESPACE_END

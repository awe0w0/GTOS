#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

#include "absl/base/internal/thread_identity.h"

static_assert(ABSL_THREAD_IDENTITY_MODE ==
              ABSL_THREAD_IDENTITY_MODE_USE_CPP11);

namespace {

using absl::base_internal::ClearCurrentThreadIdentity;
using absl::base_internal::CurrentThreadIdentityIfPresent;
using absl::base_internal::SetCurrentThreadIdentity;
using absl::base_internal::ThreadIdentity;

ThreadIdentity identities[2];
std::atomic<unsigned> attached;
std::atomic<unsigned> reclaimed[2];

void Reclaim(void* identity) {
  assert(identity == CurrentThreadIdentityIfPresent());
  assert(identity == &identities[0] || identity == &identities[1]);
  ++reclaimed[identity == &identities[1]];
  ClearCurrentThreadIdentity();
  assert(CurrentThreadIdentityIfPresent() == nullptr);
}

void Worker(unsigned index) {
  assert(CurrentThreadIdentityIfPresent() == nullptr);
  SetCurrentThreadIdentity(&identities[index], Reclaim);
  ++attached;
  while (attached.load() != 2) {
    std::this_thread::yield();
  }
  assert(CurrentThreadIdentityIfPresent() == &identities[index]);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "duplicate") == 0) {
    SetCurrentThreadIdentity(&identities[0], Reclaim);
    SetCurrentThreadIdentity(&identities[1], Reclaim);
    return 77;
  }

  assert(argc == 1);
  for (unsigned round = 1; round <= 3; ++round) {
    attached.store(0);
    std::thread a(Worker, 0);
    std::thread b(Worker, 1);
    assert(CurrentThreadIdentityIfPresent() == nullptr);
    a.join();
    b.join();
    assert(reclaimed[0].load() == round && reclaimed[1].load() == round);
  }
  std::puts("ABSEIL_CPP11_IDENTITY_PASS");
}

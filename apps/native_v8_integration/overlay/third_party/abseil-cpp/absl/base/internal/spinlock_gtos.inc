// Native GTOS part of spinlock_wait.cc. Waiters poll after a real scheduler
// yield; none are parked, so wake is intentionally unnecessary.
#include "absl/base/attributes.h"
#include "src/base/platform/gtos-native-services.h"

extern "C" {

ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockDelay)(
    std::atomic<uint32_t>* /* lock_word */, uint32_t /* value */, int loop,
    absl::base_internal::SchedulingMode /* mode */) {
  if (loop != 0) v8::base::gtos::Yield();
}

ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockWake)(
    std::atomic<uint32_t>* /* lock_word */, bool /* all */) {}

}  // extern "C"

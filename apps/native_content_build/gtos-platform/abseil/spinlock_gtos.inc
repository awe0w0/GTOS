// Native GTOS part of spinlock_wait.cc. Waiters poll after a real scheduler
// yield; none are parked, so wake is intentionally unnecessary.
#include "absl/base/attributes.h"
#include "tools/gtos_native_sdk/native-services.h"

extern "C" {

ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockDelay)(
    std::atomic<uint32_t>* /* lock_word */, uint32_t /* value */, int loop,
    absl::base_internal::SchedulingMode /* mode */) {
  if (loop == 0) return;
  unsigned call = GTOS_SYS_YIELD;
  asm volatile("int $0x80"
               : "+a"(call)
               : "b"(0u), "c"(0u)
               : "memory", "cc");
  if (call != 0) gtos::native::UnsupportedService();
}

ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockWake)(
    std::atomic<uint32_t>* /* lock_word */, bool /* all */) {}

}  // extern "C"

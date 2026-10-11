// Native IA32 adaptation of the compiler-rt emutls control ABI.
// Fixed reference: llvm-project 62397f8b3c3986f54187ce08f00b3448ea1f8880,
// compiler-rt/lib/builtins/emutls.c, LLVM Apache-2.0 WITH LLVM-exception.
//
// GTOS currently admits one user thread per private process address space.
// The native one-task capability is validated before each initial allocation.
// ABI1 admits no additional task into this private address space until Reap.
// Subsequent compiler accesses use its private cached address without a syscall.
// Storage comes from the real VM-backed heap and lasts until process Reap.
// C++ thread-local destructor registration is a separate, unresolved ABI.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "src/base/platform/gtos-native-services.h"

extern "C" int posix_memalign(void**, size_t, size_t) noexcept;

struct GtosEmutlsControl {
  uintptr_t size, alignment;
  union {
    uintptr_t index;
    void* address;
  } object;
  const void* value;
};

static_assert(sizeof(uintptr_t) == 4 && sizeof(GtosEmutlsControl) == 16,
              "Actual compiler IA32 emutls control ABI");
static_assert(offsetof(GtosEmutlsControl, object) == 8 &&
                  offsetof(GtosEmutlsControl, value) == 12,
              "Compiler-emitted emutls field offsets");

extern "C" void* __emutls_get_address(GtosEmutlsControl* control) {
  if (control == nullptr) v8::base::gtos::UnsupportedService();
  if (control->object.address != nullptr) return control->object.address;
  v8::base::gtos::CurrentProcessInfo();
  const size_t alignment =
      control->alignment < sizeof(void*) ? sizeof(void*) : control->alignment;
  if ((alignment & (alignment - 1)) != 0) {
    v8::base::gtos::UnsupportedService();
  }
  void* object = nullptr;
  if (posix_memalign(&object, alignment, control->size ? control->size : 1) != 0 ||
      object == nullptr) {
    v8::base::gtos::UnsupportedService();
  }
  unsigned char* destination = static_cast<unsigned char*>(object);
  const unsigned char* initial =
      static_cast<const unsigned char*>(control->value);
  for (size_t i = 0; i < control->size; ++i) {
    destination[i] = initial == nullptr ? 0 : initial[i];
  }
  control->object.address = object;
  return object;
}

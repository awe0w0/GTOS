// Native IA32 adaptation of the compiler-rt emutls control ABI.
// Fixed reference: llvm-project 62397f8b3c3986f54187ce08f00b3448ea1f8880,
// compiler-rt/lib/builtins/emutls.c, LLVM Apache-2.0 WITH LLVM-exception.
//
// Variable indices are shared; address arrays belong to actual native thread
// IDs. Storage is released after that thread's C++ destructors, before exit.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "src/base/platform/gtos-native-services.h"
#include "thread-context.h"

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

namespace {
struct AddressArray { uintptr_t size; };
static_assert(sizeof(AddressArray) == sizeof(void*), "Native TLS array header");
uintptr_t objects_count;
uint32_t storage_lock;
class StorageGuard {
 public:
  StorageGuard() {
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(&storage_lock, &expected, 1U, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      v8::base::gtos::Yield();
      expected = 0;
    }
  }
  ~StorageGuard() { __atomic_store_n(&storage_lock, 0U, __ATOMIC_RELEASE); }
  StorageGuard(const StorageGuard&) = delete;
  StorageGuard& operator=(const StorageGuard&) = delete;
};
void** Data(AddressArray* array) {
  return reinterpret_cast<void**>(array + 1);
}
AddressArray* Grow(AddressArray* array, uintptr_t index) {
  const uintptr_t maximum = (~uintptr_t(0) - sizeof(AddressArray)) / sizeof(void*);
  if (index == 0 || index > maximum - 16) abort();
  const uintptr_t count = ((index + 1 + 15) & ~uintptr_t(15)) - 1;
  const uintptr_t previous = array == nullptr ? 0 : array->size;
  if (count < index || count > maximum || previous > count) abort();
  const size_t bytes = sizeof(AddressArray) + count * sizeof(void*);
  void* storage = array == nullptr ? malloc(bytes) : realloc(array, bytes);
  if (storage == nullptr) abort();
  array = static_cast<AddressArray*>(storage);
  for (uintptr_t i = previous; i < count; ++i) Data(array)[i] = nullptr;
  array->size = count;
  return array;
}
}
extern "C" void* __emutls_get_address(GtosEmutlsControl* control) {
  if (control == nullptr) v8::base::gtos::UnsupportedService();
  auto& state = v8::base::gtos::CurrentNativeThreadState();
  StorageGuard guard;
  uintptr_t index = __atomic_load_n(&control->object.index, __ATOMIC_ACQUIRE);
  if (index == 0) {
    if (objects_count == ~uintptr_t(0)) abort();
    index = ++objects_count;
    __atomic_store_n(&control->object.index, index, __ATOMIC_RELEASE);
  } else if (index > objects_count) {
    v8::base::gtos::UnsupportedService();
  }
  auto* array = static_cast<AddressArray*>(state.tls_array);
  if (array == nullptr || index > array->size) {
    array = Grow(array, index);
    state.tls_array = array;
  }
  void*& object = Data(array)[index - 1];
  if (object != nullptr) return object;
  const size_t alignment =
      control->alignment < sizeof(void*) ? sizeof(void*) : control->alignment;
  if ((alignment & (alignment - 1)) != 0) abort();
  if (posix_memalign(&object, alignment, control->size ? control->size : 1) != 0 ||
      object == nullptr) abort();
  auto* destination = static_cast<unsigned char*>(object);
  const auto* initial = static_cast<const unsigned char*>(control->value);
  for (size_t i = 0; i < control->size; ++i)
    destination[i] = initial == nullptr ? 0 : initial[i];
  return object;
}
extern "C" void gtos_native_emutls_cleanup() noexcept {
  auto& state = v8::base::gtos::CurrentNativeThreadState();
  StorageGuard guard;
  auto* array = static_cast<AddressArray*>(state.tls_array);
  if (array == nullptr) return;
  state.tls_array = nullptr;
  for (uintptr_t i = 0; i < array->size; ++i) free(Data(array)[i]);
  free(array);
}

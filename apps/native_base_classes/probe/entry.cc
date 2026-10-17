#include "api.h"
#include <stdlib.h>
extern "C" int gtos_native_environment_initialize() noexcept;
extern "C" int gtos_native_heap_enable_large_allocations() noexcept;
using Initializer = void (*)();
extern "C" Initializer __init_array_start[], __init_array_end[];
extern "C" {
volatile NativeClassRecord native_class_record
    __attribute__((section(".data.class_record"))) = {
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}};
// Existing real process-exit provider exports this historical diagnostic name.
// This separate class probe contains no V8 main and makes no V8 runtime claim.
volatile unsigned native_v8_integration_stage = 0;
volatile unsigned native_v8_integration_result = 0xffffffffU;
[[noreturn]] void NativeClassEntry() {
  native_class_record.stage = 1;
  if (gtos_native_environment_initialize() != 0) {
    native_class_record.error = 1;
    exit(1);
  }
  if (gtos_native_heap_enable_large_allocations() != 0) {
    native_class_record.error = 2;
    exit(2);
  }
  for (Initializer* current = __init_array_start; current != __init_array_end;
       ++current) {
    (*current)();
    native_class_record.constructors = native_class_record.constructors + 1;
  }
  native_class_record.stage = 2;
  const unsigned result = NativeBaseClassChecks(&native_class_record);
  native_class_record.result = result;
  native_class_record.stage = 3;
  exit(static_cast<int>(result));
}
}

#include "api.h"
#include <stdlib.h>
#ifndef GTOS_CLIPBOARD_MODE
#define GTOS_CLIPBOARD_MODE 0
#endif
static_assert(GTOS_CLIPBOARD_MODE == 0 || GTOS_CLIPBOARD_MODE == 1);
extern "C" int gtos_native_environment_initialize() noexcept;
extern "C" int gtos_native_heap_enable_large_allocations() noexcept;
using Initializer = void (*)();
extern "C" Initializer __init_array_start[], __init_array_end[];
extern "C" {
volatile NativeClipboardRecord native_clipboard_record
    __attribute__((section(".data.clipboard_record"))) = {
        1, GTOS_CLIPBOARD_MODE, 0, 0, 0, 0, 0xffffffffU, 0,
        0, 0, 0, 0, 0, 0, 0, 0, {}};
// The real process-exit provider uses these historical diagnostic symbols.
// This clipboard probe contains no V8 main and makes no V8 runtime claim.
volatile unsigned native_v8_integration_stage = 0;
volatile unsigned native_v8_integration_result = 0xffffffffU;
[[noreturn]] void NativeClipboardEntry() {
  native_clipboard_record.stage = 1;
  if (gtos_native_environment_initialize() != 0) {
    native_clipboard_record.error = 1;
    exit(1);
  }
  if (gtos_native_heap_enable_large_allocations() != 0) {
    native_clipboard_record.error = 2;
    exit(2);
  }
  for (Initializer* current = __init_array_start; current != __init_array_end;
       ++current) {
    (*current)();
    native_clipboard_record.constructors =
        native_clipboard_record.constructors + 1;
  }
  native_clipboard_record.stage = 2;
  const uint32_t result = NativeClipboardChecks(&native_clipboard_record);
  native_clipboard_record.result = result;
  native_clipboard_record.stage = 3;
  exit(static_cast<int>(result));
}
}

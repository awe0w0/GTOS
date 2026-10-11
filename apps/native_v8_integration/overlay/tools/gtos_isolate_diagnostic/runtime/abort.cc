#include <stdlib.h>
#include "heap.h"

extern "C" [[noreturn]] void abort() noexcept {
  gtos_native_heap_panic(0x21U);
}

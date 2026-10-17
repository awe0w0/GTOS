#include "api.h"
extern "C" unsigned __wrap_NativeClassLength(const char*, unsigned) {
  // Deliberately wrong flat diagnostic API, never a replacement libc symbol.
  return 1;
}

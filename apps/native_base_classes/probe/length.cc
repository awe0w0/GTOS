#include "api.h"
#include <stddef.h>
#include <string.h>
static_assert(sizeof(size_t) == sizeof(unsigned));
extern "C" unsigned NativeClassLength(const char* text, unsigned maximum) {
  // The caller is a separate translation unit. Keep the actual LLVM call.
  size_t (*volatile function)(const char*, size_t) = &strnlen;
  return function(text, maximum);
}

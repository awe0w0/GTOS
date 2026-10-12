#ifndef GTOS_NATIVE_DIAGNOSTIC_OUTPUT_H
#define GTOS_NATIVE_DIAGNOSTIC_OUTPUT_H
#include <stdarg.h>

// Real native console output, with the pinned LLVM printf conversion rules.
// No FILE object or heap allocation is needed by the output callback.
int gtos_native_vprint_error(const char* format, va_list arguments) noexcept;
#endif

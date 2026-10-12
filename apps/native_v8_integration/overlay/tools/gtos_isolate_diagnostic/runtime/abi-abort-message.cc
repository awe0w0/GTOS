#include "abort_message.h"
#include <stdarg.h>
#include <stdlib.h>
#include "diagnostic-output.h"

#if !defined(__GTOS__) || GTOS_ISOLATE_COMPILE_DIAGNOSTIC != 1
#error "GTOS ABI termination requires the explicit native diagnostic profile"
#endif

extern "C" [[noreturn]] void __abort_message(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    (void)gtos_native_vprint_error(format, arguments);
    va_end(arguments);
    // Recursive initialization must terminate even when diagnostic output fails.
    abort();
}

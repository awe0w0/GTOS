#include <__config>
#include <__verbose_abort>
#include <cstdarg>
#include <cstdlib>
#include "diagnostic-output.h"

#if !defined(__GTOS__) || GTOS_ISOLATE_COMPILE_DIAGNOSTIC != 1
#error "GTOS verbose termination requires the explicit native diagnostic profile"
#endif

_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
void __libcpp_verbose_abort(const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    (void)gtos_native_vprint_error(format, arguments);
    va_end(arguments);
    // A failed diagnostic write must still terminate the process.
    std::abort();
}
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD

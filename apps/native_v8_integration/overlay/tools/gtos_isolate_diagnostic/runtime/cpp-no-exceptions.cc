#include <__config>
#include <exception>
#include <new>
#include "heap.h"

#if !defined(__GTOS__) || GTOS_ISOLATE_COMPILE_DIAGNOSTIC != 1 || _LIBCPP_HAS_EXCEPTIONS
#error "GTOS native C++ runtime requires the explicit no-exception diagnostic profile"
#endif

// This profile has no C++ exception unwinder. Faults and allocation failure
// terminate the process; they cannot leave an in-flight C++ exception.
namespace std {
int uncaught_exceptions() noexcept { return 0; }

[[noreturn]] void __throw_bad_alloc() {
    gtos_native_heap_panic(GTOS_NATIVE_HEAP_EXIT_NEW_OOM);
}
} // namespace std

#ifndef GTOS_NATIVE_THREAD_EXIT_H
#define GTOS_NATIVE_THREAD_EXIT_H

// Actual compiler-generated C++ TLS destructor registration for native ABI1.
// The executable owns one user task in its private address space. There is no
// DSO unloading or shared-address-space thread creation in this runtime.
extern "C" {
int __cxa_thread_atexit(void (*destroy)(void*), void* object, void* dso) noexcept;
void gtos_native_thread_cleanup() noexcept;
[[noreturn]] void gtos_native_exit_with_tls(unsigned code) noexcept;
}
#endif

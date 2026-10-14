#ifndef GTOS_NATIVE_THREAD_EXIT_H
#define GTOS_NATIVE_THREAD_EXIT_H

// Actual compiler-generated C++ TLS destructor registration for native ABI1.
// Cleanup runs the calling thread's destructors before releasing its TLS and
// errno cell. Workers must use this exit hook, then the kernel THREAD_EXIT;
// main-task exit uses the existing process/group EXIT. No DSO unloading.
extern "C" {
int __cxa_thread_atexit(void (*destroy)(void*), void* object, void* dso) noexcept;
void gtos_native_thread_cleanup() noexcept;
void gtos_native_emutls_cleanup() noexcept;
[[noreturn]] void gtos_native_exit_with_tls(unsigned code) noexcept;
}
#endif

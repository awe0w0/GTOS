#include "thread-exit.h"
#include "thread-context.h"
#include <process/thread_abi.h>
#include <new>
#include <stdlib.h>

#include "src/base/platform/gtos-native-services.h"

namespace {
struct Destructor {
  void (*destroy)(void*);
  void* object;
  void* dso;
  Destructor* next;
};
}

extern "C" {
void* __dso_handle = &__dso_handle;
}

extern "C" int __cxa_thread_atexit(void (*destroy)(void*), void* object,
                                  void* dso) noexcept {
  if (destroy == nullptr) return -1;
  auto& state = v8::base::gtos::CurrentNativeThreadState();
  void* storage = malloc(sizeof(Destructor));
  if (storage == nullptr) return -1;
  state.destructors = ::new (storage) Destructor{
      destroy, object, dso, static_cast<Destructor*>(state.destructors)};
  return 0;
}

extern "C" void gtos_native_thread_cleanup() noexcept {
  auto& state = v8::base::gtos::CurrentNativeThreadState();
  if (state.cleaning) v8::base::gtos::UnsupportedService();
  state.cleaning = true;
  while (state.destructors != nullptr) {
    auto* item = static_cast<Destructor*>(state.destructors);
    state.destructors = item->next;
    void (*destroy)(void*) = item->destroy;
    void* object = item->object;
    // Remove the entry before the callback: a destructor can register another
    // destructor, including by initializing another thread_local object.
    item->~Destructor();
    free(item);
    destroy(object);
  }
  gtos_native_emutls_cleanup();
  state.cleaning = false;
  v8::base::gtos::ReleaseNativeThreadState(state);
}

extern "C" [[noreturn]] void gtos_native_exit_with_tls(unsigned code) noexcept {
  const auto info = v8::base::gtos::CurrentNativeThreadInfo();
  gtos_native_thread_cleanup();
  unsigned call = info.thread_id == info.process_id ? GTOS_SYS_EXIT : GTOS_SYS_THREAD_EXIT;
  asm volatile("int $0x80" : "+a"(call) : "b"(code), "c"(0U) : "memory", "cc");
  v8::base::gtos::UnsupportedService();
}

#include "thread-exit.h"
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
Destructor* destructors;
bool cleaning;
}

extern "C" {
void* __dso_handle = &__dso_handle;
}

extern "C" int __cxa_thread_atexit(void (*destroy)(void*), void* object,
                                  void* dso) noexcept {
  if (destroy == nullptr) return -1;
  v8::base::gtos::CurrentProcessInfo();
  void* storage = malloc(sizeof(Destructor));
  if (storage == nullptr) return -1;
  destructors = ::new (storage) Destructor{destroy, object, dso, destructors};
  return 0;
}

extern "C" void gtos_native_thread_cleanup() noexcept {
  if (cleaning) v8::base::gtos::UnsupportedService();
  cleaning = true;
  while (destructors != nullptr) {
    Destructor* item = destructors;
    destructors = item->next;
    void (*destroy)(void*) = item->destroy;
    void* object = item->object;
    // Remove the entry before the callback: a destructor can register another
    // destructor, including by initializing another thread_local object.
    item->~Destructor();
    free(item);
    destroy(object);
  }
  cleaning = false;
}

extern "C" [[noreturn]] void gtos_native_exit_with_tls(unsigned code) noexcept {
  gtos_native_thread_cleanup();
  unsigned call = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(call) : "b"(code), "c"(0U) : "memory", "cc");
  v8::base::gtos::UnsupportedService();
}

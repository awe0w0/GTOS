#include <stdlib.h>
#include "thread-exit.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/__support/File/file.h"
#include "src/stdio/fclose.h"
#include "src/stdio/stdin.h"
#include "src/stdio/stdout.h"
#include "src/stdio/stderr.h"

namespace {
bool exiting;
}

using Initializer = void (*)();
extern "C" Initializer __fini_array_start[], __fini_array_end[];
extern "C" void __cxa_finalize(void*);
extern "C" volatile unsigned native_v8_integration_stage;
extern "C" volatile unsigned native_v8_integration_result;

extern "C" [[noreturn]] void exit(int status) noexcept {
  v8::base::gtos::CurrentProcessInfo();
  if (exiting) v8::base::gtos::UnsupportedService();
  exiting = true;
  native_v8_integration_result = static_cast<unsigned>(status);
  native_v8_integration_stage = 3;
  gtos_native_thread_cleanup();
  __cxa_finalize(nullptr);
  for (Initializer* p = __fini_array_end; p != __fini_array_start;)
    (*--p)();
  // A fini function can register a new callback. Already finalized entries
  // have been removed, so this cannot call an old destructor twice.
  __cxa_finalize(nullptr);
  using __llvm_libc_cr::File;
  while (File* file = File::get_first_file())
    __llvm_libc_cr::fclose(reinterpret_cast<::FILE*>(file));
  // Constant-initialized standard streams are not in the dynamic FILE list.
  if (__llvm_libc_cr::stdin) __llvm_libc_cr::fclose(__llvm_libc_cr::stdin);
  if (__llvm_libc_cr::stdout) __llvm_libc_cr::fclose(__llvm_libc_cr::stdout);
  if (__llvm_libc_cr::stderr) __llvm_libc_cr::fclose(__llvm_libc_cr::stderr);
  native_v8_integration_stage = 4;
  unsigned call = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(call)
               : "b"(static_cast<unsigned>(status)), "c"(0U) : "memory", "cc");
  v8::base::gtos::UnsupportedService();
}

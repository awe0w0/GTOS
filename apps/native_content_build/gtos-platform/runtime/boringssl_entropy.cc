// BoringSSL system entropy for native GTOS IA32 processes.
#include "third_party/boringssl/src/crypto/rand/internal.h"

#if defined(OPENSSL_RAND_GTOS)
#include <stdlib.h>

#include "third_party/boringssl/src/crypto/bcm_support.h"
#include "tools/gtos_native_sdk/native-services.h"

namespace {

bool HasHardwareEntropy() {
  uint32_t a, b, c, d;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
               : "a"(0u), "c"(0u));
  if (a < 1)
    return false;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
               : "a"(1u), "c"(0u));
  return (c & (1u << 30)) != 0;
}

[[noreturn]] void EntropyUnavailable() {
  constexpr char message[] = "GTOS BoringSSL hardware entropy unavailable\n";
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(call) : "b"(message), "c"(sizeof(message) - 1) : "memory", "cc");
  abort();
}

}  // namespace

void bssl::CRYPTO_init_sysrand() {}  // RDRAND has no process resource to open.

void bssl::CRYPTO_sysrand(uint8_t* out, size_t len) {
  if (!len)
    return;
  const auto info = gtos::native::CurrentProcessInfo();
  const auto address = reinterpret_cast<uintptr_t>(out);
  if (address < info.user_begin || address >= info.user_end ||
      len > info.user_end - address || !HasHardwareEntropy())
    EntropyUnavailable();

  // Keep the native V8 policy of at most ten attempts per word. BoringSSL
  // requires process termination when a request cannot be filled completely.
  while (len) {
    uint32_t word;
    uint8_t ready = 0;
    for (unsigned attempt = 0; attempt != 10 && !ready; ++attempt)
      asm volatile("rdrand %0; setc %1" : "=r"(word), "=qm"(ready) : : "cc");
    if (!ready)
      EntropyUnavailable();
    for (unsigned i = 0; i < sizeof(word) && len; ++i, --len) {
      *out++ = static_cast<uint8_t>(word);
      word >>= 8;
    }
  }
}
#endif  // OPENSSL_RAND_GTOS

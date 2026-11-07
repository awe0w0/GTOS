#include "memory_record.h"
#include "md5-vectors.h"

#include <assert.h>
#include <process/abi.h>
#include <string.h>

#include "third_party/boringssl/src/crypto/internal.h"

#if !defined(OPENSSL_GTOS_THREADS) || !defined(OPENSSL_THREADS) || defined(OPENSSL_NO_ASM)
#error The consumer needs the native threaded and assembly-enabled BoringSSL.
#endif

extern "C" unsigned native_crypto_md5(const unsigned char*, unsigned, unsigned,
                                      unsigned char*);

extern "C" {
__attribute__((section(".data.vm_record"), used))
volatile gtos_perfetto_probe::Record native_memory_record = {
    1, GTOS_BORINGSSL_GUEST_MODE, 0, 0, 0, 0, 0};
}

namespace {
unsigned initialized;
void Initialize() { ++initialized; }

void Check(bool ok) {
  native_memory_record.checks = native_memory_record.checks + 1;
  if (!ok)
    native_memory_record.failures = native_memory_record.failures + 1;
}

bool CheckDigest(const unsigned char* bytes, unsigned length,
                 const unsigned char* expected) {
  unsigned char output[18];
  for (unsigned stride : {0u, 13u}) {
    output[0] = 0x9f;
    output[17] = 0x2b;
    if (!native_crypto_md5(bytes, length, stride, output + 1) ||
        memcmp(output + 1, expected, 16) != 0 ||
        output[0] != 0x9f || output[17] != 0x2b)
      return false;
  }
  return true;
}
}  // namespace

extern "C" int NativeEntry() {
  constexpr char begin[] = "CONTENT BORINGSSL BEGIN\n";
  unsigned call = GTOS_SYS_WRITE;
  asm volatile("int $0x80"
               : "+a"(call) : "b"(begin), "c"(sizeof(begin) - 1) : "memory", "cc");
  if constexpr (GTOS_BORINGSSL_GUEST_MODE == 2) {
    native_memory_record.stage = 1;
    assert(false && "intentional GTOS assertion");
    return 3;
  }
  bssl::StaticMutex lock;
  lock.LockRead();
  if constexpr (GTOS_BORINGSSL_GUEST_MODE == 1) {
    native_memory_record.stage = 1;
    lock.LockWrite();
    return 2;
  }
  lock.LockRead();
  lock.UnlockRead();
  lock.UnlockRead();
  lock.LockWrite();
  bssl::CRYPTO_once_t once = CRYPTO_ONCE_INIT;
  bssl::CRYPTO_once(&once, Initialize);
  bssl::CRYPTO_once(&once, Initialize);
  Check(initialized == 1);
  lock.UnlockWrite();

  for (const auto& vector : nativeCryptoMd5RfcVectors) {
    Check(CheckDigest(reinterpret_cast<const unsigned char*>(vector.input),
                      vector.length, vector.digest));
  }
  unsigned char input[4097];
  for (unsigned i = 0; i < sizeof(input); ++i)
    input[i] = (i * 37 + 11) & 255;
  for (const auto& vector : nativeCryptoMd5BinaryVectors)
    Check(CheckDigest(input, vector.length, vector.digest));
#if defined(GTOS_BORINGSSL_GUEST_WRONG_EXPECTATION)
  unsigned char wrong_digest[16] = {};
  Check(CheckDigest(input, 0, wrong_digest));
#endif
  native_memory_record.stage = 2;
  return native_memory_record.failures ? 1 : 0;
}

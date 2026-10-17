#include <openssl/md5.h>

// Keep all C++23 BoringSSL types inside this translation unit. The independent
// native consumer passes only byte buffers and 32-bit C arguments.
extern "C" unsigned native_crypto_md5(const unsigned char *input, unsigned length,
                                      unsigned stride, unsigned char *output) {
  if (stride == 0) {
    return MD5(input, length, output) == output;
  }
  MD5_CTX context;
  if (!MD5_Init(&context) || !MD5_Update(&context, nullptr, 0)) {
    return 0;
  }
  unsigned offset = 0;
  while (offset < length) {
    const unsigned count = length - offset < stride ? length - offset : stride;
    if (!MD5_Update(&context, input + offset, count) ||
        !MD5_Update(&context, nullptr, 0)) {
      return 0;
    }
    offset += count;
  }
  return MD5_Final(output, &context);
}

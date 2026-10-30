/*
 * The four symbols a 64 KiB-bounded GTOS module needs, and nothing else.
 *
 * The guest kernel refuses any native module larger than 64 KiB (src/kernel.cpp:843), so the file-subset image that links with the full libc
 * closure and printf runtime — 244 KiB — can never be loaded, however correct it is. This is the same subset cut down to what the loader path
 * actually references: errno, memcmp, memset, strlen. Nothing here formats output, allocates, or touches the FILE layer, so the image stays in
 * the low tens of kilobytes.
 *
 * errno: the LLVM libc objects call `__llvm_libc_errno`, which returns a pointer to the per-thread error slot. GTOS compiles with
 * -femulated-tls, so a thread-local int is the right shape; it is declared hidden because nothing outside this image should reach it.
 */
extern "C" {

// One process-global slot: this probe image is single-threaded, and __thread would pull __emutls_get_address, which the freestanding link does
// not have. A multi-threaded consumer must supply the per-thread form (the qualified FILE library carries one).
static int gtos_errno_storage;

int *__llvm_libc_errno(void) { return &gtos_errno_storage; }

void *memcpy(void *destination, const void *source, unsigned bytes) {
  unsigned char *out = (unsigned char *)destination;
  const unsigned char *in = (const unsigned char *)source;
  for (unsigned index = 0; index < bytes; ++index) out[index] = in[index];
  return destination;
}

void *memset(void *destination, int value, unsigned bytes) {
  unsigned char *out = (unsigned char *)destination;
  for (unsigned index = 0; index < bytes; ++index) out[index] = (unsigned char)value;
  return destination;
}

int memcmp(const void *left, const void *right, unsigned bytes) {
  const unsigned char *a = (const unsigned char *)left;
  const unsigned char *b = (const unsigned char *)right;
  for (unsigned index = 0; index < bytes; ++index) {
    if (a[index] != b[index]) return (int)a[index] - (int)b[index];
  }
  return 0;
}

unsigned strlen(const char *text) {
  unsigned length = 0;
  while (text[length] != 0) ++length;
  return length;
}

} // extern "C"

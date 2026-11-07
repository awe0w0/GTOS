#include <stddef.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

/* Freestanding allocator for a single completed stream at a time. */
static _Alignas(16) unsigned char arena[1024 * 1024];
static size_t used;

void* malloc(size_t bytes) {
  if (bytes > sizeof(arena) - used)
    return NULL;
  void* result = arena + used;
  used += (bytes + 15) & ~(size_t)15;
  return result;
}

void free(void* ptr) { (void)ptr; }

_Noreturn void exit(int status) {
  __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory", "cc");
  __builtin_trap();
}

/* The formal debug archive can reference diagnostics. Fail if it uses them. */
FILE* stderr;
int fprintf(FILE* stream, const char* format, ...) {
  (void)stream;
  (void)format;
  exit(60);
}
int putc(int value, FILE* stream) {
  (void)value;
  (void)stream;
  exit(61);
}
int isgraph(int value) {
  (void)value;
  exit(62);
}

int zlib_test_main(void) {
  static const unsigned char check[] = "123456789";
  static unsigned char input[4096], encoded[8192], output[4098];
  for (unsigned i = 0; i < sizeof(input); ++i)
    input[i] = (unsigned char)(i % 37 + i % 13);

#if defined(GTOS_ZLIB_WRONG_CRC)
  const uLong expected_crc = 0xcbf43927;
#else
  const uLong expected_crc = 0xcbf43926;
#endif
  if (crc32(0, check, sizeof(check) - 1) != expected_crc ||
      adler32(1, check, sizeof(check) - 1) != 0x091e01de)
    return 2;

  uLongf compressed = sizeof(encoded);
  if (compress2(encoded, &compressed, input, sizeof(input), 6) != Z_OK ||
      compressed < 8 || compressed >= sizeof(input))
    return 3;

  used = 0;
  output[0] = 0x17;
  output[sizeof(output) - 1] = 0x9a;
  uLongf decoded = sizeof(input);
  if (uncompress(output + 1, &decoded, encoded, compressed) != Z_OK ||
      decoded != sizeof(input) || memcmp(input, output + 1, decoded) ||
      output[0] != 0x17 || output[sizeof(output) - 1] != 0x9a)
    return 4;

  used = 0;
  decoded = sizeof(input);
  encoded[compressed - 1] ^= 1;
  if (uncompress(output + 1, &decoded, encoded, compressed) != Z_DATA_ERROR ||
      output[0] != 0x17 || output[sizeof(output) - 1] != 0x9a)
    return 5;
  return 0;
}

/* Only the host test harness uses the Linux i386 exit instruction. */
__asm__(".globl _start\n_start:\n"
        "andl $-16,%esp\n"
        "call zlib_test_main\n"
        "movl %eax,%ebx\nmovl $1,%eax\nint $0x80\nud2\n");

#include <stddef.h>
#include <stdint.h>
// Real byte operations for this integer-only freestanding module. Compile
// with -fno-builtin to prevent optimizing their loops into recursive calls.
void* memcpy(void* restrict dst, const void* restrict src, size_t count) {
  unsigned char* d=dst; const unsigned char* s=src;
  for (size_t i=0;i<count;i++) d[i]=s[i];
  return dst;
}
void* memset(void* dst, int value, size_t count) {
  unsigned char* d=dst;
  for (size_t i=0;i<count;i++) d[i]=(unsigned char)value;
  return dst;
}
void* memmove(void* dst, const void* src, size_t count) {
  unsigned char* d=dst; const unsigned char* s=src;
  if ((uintptr_t)d < (uintptr_t)s) {
    for (size_t i=0;i<count;i++) d[i]=s[i];
  } else {
    for (size_t i=count;i;i--) d[i-1]=s[i-1];
  }
  return dst;
}
int memcmp(const void* first, const void* second, size_t count) {
  const unsigned char* a=first; const unsigned char* b=second;
  for (size_t i=0;i<count;i++) if(a[i]!=b[i]) return a[i]<b[i]?-1:1;
  return 0;
}

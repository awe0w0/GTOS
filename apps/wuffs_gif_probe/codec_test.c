#include "gif_codec.h"
#include "fixtures.h"
#include <stdint.h>
#include <string.h>
#ifdef GTOS_CODEC_HOST_TEST
#include <stdio.h>
#endif

static _Alignas(8) union {
  uint64_t alignment;
  unsigned char bytes[131104];
} context;
static unsigned char pixels[48];
static unsigned char scratch[2048];
static int same(const unsigned char* a, const unsigned char* b, size_t n) {
  for(size_t i=0;i<n;i++) if(a[i]!=b[i]) return 0;
  return 1;
}
static int guards(size_t used) {
  for(size_t i=0;i<8;i++) if(context.bytes[i]!=0xa5 || pixels[i]!=0xa5) return 0;
  for(size_t i=8+used;i<sizeof(context.bytes);i++) if(context.bytes[i]!=0xa5) return 0;
  for(size_t i=24;i<sizeof(pixels);i++) if(pixels[i]!=0xa5) return 0;
  return 1;
}
static int decode(const unsigned char* data, size_t n, size_t capacity,
                  unsigned int* w, unsigned int* h) {
  size_t need=gtos_gif_context_size();
  memset(context.bytes,0xa5,sizeof(context.bytes));
  memset(pixels,0xa5,sizeof(pixels));
  return gtos_gif_first_frame(context.bytes+8,need,data,n,
                               pixels+8,capacity,w,h);
}
int gtos_gif_selftest(void) {
  size_t need=gtos_gif_context_size();
  if(need>sizeof(context.bytes)-16) return 10;
  if((uintptr_t)context.bytes&7) return 28;
  unsigned int w=0,h=0;
  if(gtos_gif_first_frame(context.bytes+9,need,gif_valid,sizeof(gif_valid),
                          pixels+8,16,&w,&h)!=1) return 29;
  if(decode(gif_valid,sizeof(gif_valid),16,&w,&h) || w!=2 || h!=2 ||
     !same(pixels+8,bgra_valid,16) || !guards(need)) return 11;
  if(decode(gif_transparent,sizeof(gif_transparent),16,&w,&h) ||
     w!=2 || h!=2 || !same(pixels+8,bgra_transparent,16) || !guards(need)) return 12;
  // Real two-frame input; this adapter intentionally decodes only frame one.
  if(decode(gif_animated,sizeof(gif_animated),16,&w,&h) ||
     !same(pixels+8,bgra_valid,16) || !guards(need)) return 13;
  for(size_t capacity=0;capacity<16;capacity++) {
    w=0x12345678; h=0x87654321;
    if(!decode(gif_valid,sizeof(gif_valid),capacity,&w,&h) ||
       w!=0x12345678 || h!=0x87654321 || !guards(need)) return 14;
    for(size_t i=8;i<24;i++) if(pixels[i]!=0xa5) return 15;
  }
  for(size_t n=0;n<=sizeof(gif_valid);n++) {
    w=0;h=0;
    int result=decode(gif_valid,n,16,&w,&h);
    if(!guards(need)) return 16;
    if(!result && (w!=2 || h!=2 || !same(pixels+8,bgra_valid,16))) return 17;
  }
  memcpy(scratch,gif_valid,sizeof(gif_valid));
  scratch[0]='X';
  if(!decode(scratch,sizeof(gif_valid),16,&w,&h) || !guards(need)) return 18;
  // Hostile declared dimensions must be checked before output binding.
  memcpy(scratch,gif_valid,sizeof(gif_valid));
  scratch[6]=scratch[7]=scratch[8]=scratch[9]=0xff;
  if(!decode(scratch,sizeof(gif_valid),16,&w,&h) || !guards(need)) return 19;
  memset(context.bytes,0xa5,sizeof(context.bytes));
  if(gtos_gif_first_frame(context.bytes+8,need-1,gif_valid,sizeof(gif_valid),
                          pixels+8,16,&w,&h)!=1) return 20;
  for(size_t i=0;i<sizeof(context.bytes);i++) if(context.bytes[i]!=0xa5) return 21;
  // Deterministic bounded mutations of a real GIF under host ASan/UBSan.
  uint32_t random=0x47544f53;
  for(unsigned int iteration=0;iteration<2000;iteration++) {
    memcpy(scratch,gif_valid,sizeof(gif_valid));
    random=random*1664525u+1013904223u;
    size_t index=random%sizeof(gif_valid);
    scratch[index]^=(unsigned char)(1u<<((random>>24)&7));
    w=0;h=0;
    int result=decode(scratch,sizeof(gif_valid),16,&w,&h);
    if(!guards(need)) return 22;
    if(!result && (!w || !h || w>4 || h>4 || (size_t)w*h>4)) return 23;
  }
  // Overlap, identity, and zero-length behavior of real memory primitives.
  for(unsigned int i=0;i<64;i++) scratch[i]=(unsigned char)i;
  memmove(scratch+7,scratch,40);
  for(unsigned int i=0;i<40;i++) if(scratch[i+7]!=i) return 24;
  memmove(scratch,scratch+7,40);
  for(unsigned int i=0;i<40;i++) if(scratch[i]!=i) return 25;
  memmove(scratch,scratch,40); memcpy(scratch+64,scratch,40);
  if(memcmp(scratch,scratch+64,40) || memcmp(scratch,scratch,0)) return 26;
  if(!same(gif_valid,gif_valid_before,sizeof(gif_valid))) return 27;
  return 0;
}
#ifdef GTOS_CODEC_HOST_TEST
int main(void) {
  int result=gtos_gif_selftest();
  printf("WUFFS GIF GTOS COMPONENT %s context_bytes=%zu mutations=2000 result=%d\n",
         result?"FAIL":"PASS",gtos_gif_context_size(),result);
  return result;
}
#endif

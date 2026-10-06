#if !defined(__i386__) || defined(__linux__)
#error This app needs the existing freestanding GTOS i386 ABI1, not Linux.
#endif
_Static_assert(sizeof(void*) == 4 && sizeof(unsigned int) == 4, "32-bit app");
extern int gtos_gif_selftest(void);
static volatile unsigned int initialized = 0x47544f53;
static volatile unsigned int zeroed;
static int call(unsigned int n,unsigned int a,unsigned int b) {
  int result;
  __asm__ volatile("int $0x80" : "=a"(result) : "a"(n),"b"(a),"c"(b) : "memory","cc");
  return result;
}
int codec_guest_main(void) {
  static const char begin[]="GTOS WUFFS GIF START SCALAR 2000 MUTATIONS ABI1\n";
  static const char pass[]="GTOS WUFFS GIF PASS PIXELS BOUNDS 2000 MUTATIONS ABI1\n";
  if(call(0x4700,0,0)!=1) return 10;
  if(initialized!=0x47544f53 || zeroed) return 11;
  zeroed=0x434f4445;
  if(call(0x4703,0,0) || zeroed!=0x434f4445 || initialized!=0x47544f53) return 12;
  if(call(0x4701,(unsigned int)begin,sizeof(begin)-1)!=sizeof(begin)-1) return 13;
  int result=gtos_gif_selftest();
  if(result) return 20+result;
  if(call(0x4701,(unsigned int)pass,sizeof(pass)-1)!=sizeof(pass)-1) return 14;
  return 0;
}

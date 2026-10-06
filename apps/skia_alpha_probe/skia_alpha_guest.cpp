#include "skia_pixel.h"
#if defined(__linux__) || defined(_WIN32)
#error GTOS native component must not inherit a host OS identity
#endif
static_assert(sizeof(void*)==4,"GTOS i386 ABI1");
static int call(unsigned n,unsigned a,unsigned b) {
    int result;asm volatile("int $0x80":"=a"(result):"a"(n),"b"(a),"c"(b):"memory","cc");return result;
}
extern "C" void gtos_skia_assert_failure(void) {call(0x4704,0x534bffff,0);asm volatile("ud2");__builtin_unreachable();}
static volatile unsigned cursor,initialized=0x47544f53;
static uint8_t src[256],old[256],dst[256];
#define CHECK(c,id) do {if (!(c)) return 0x10000000U|(cursor<<8)|(id);} while(false)
extern "C" int skia_guest_main(void) {
    using namespace gtos_skia;
    static const char begin[]="GTOS SKIA ALPHA START 65536 PAIRS 2000 PIXEL CASES ABI1\n";
    static const char pass[]="GTOS SKIA ALPHA PASS 65536 PAIRS 2000 PIXEL CASES BOUNDS ABI1\n";
    CHECK(call(0x4700,0,0)==1 && !cursor && initialized==0x47544f53,1);
    CHECK(call(0x4701,(unsigned)begin,sizeof(begin)-1)==(int)sizeof(begin)-1,2);
    for (unsigned a=0;a<256;++a) {
        for (unsigned c=0;c<256;++c) {CHECK(SkMulDiv255Round(c,a)==(c*a+127)/255,3);++cursor;}
        if (!(a%16)) CHECK(call(0x4703,0,0)==0 && initialized==0x47544f53,4);
    }
    CHECK(cursor==65536,5);uint32_t random=0x47544f53;
    for (cursor=0;cursor<2000;++cursor) {
        random=random*1664525U+1013904223U;const uint32_t w=1+random%7;
        random=random*1664525U+1013904223U;const uint32_t h=1+random%5;
        const uint32_t ss=w*4+cursor%9,ds=w*4+(cursor*3)%9;
        for (unsigned i=0;i<256;++i) {random=random*1664525U+1013904223U;src[i]=(uint8_t)(random>>24);}
        const uint8_t alphas[]={0,1,7,127,128,254,255,33};
        for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x)
            if (cursor<8) src[y*ss+x*4+3]=alphas[cursor];
        for (unsigned i=0;i<256;++i) {old[i]=src[i];dst[i]=0xa5;}
        CHECK(PremultiplyBgraRows(src,256,ss,dst,256,ds,w,h)==PixelOk,6);
        for (unsigned i=0;i<256;++i) {
            CHECK(src[i]==old[i],7);const uint32_t y=i/ds,x=i%ds;
            if (y>=h || x>=w*4) {CHECK(dst[i]==0xa5,8);continue;}
            const uint32_t at=y*ss+(x/4)*4;const unsigned channel=x%4;
            CHECK(dst[i]==(channel==3 ? src[at+3] : (src[at+2-channel]*src[at+3]+127)/255),9);
        }
        if (!(cursor%32)) CHECK(call(0x4703,0,0)==0 && initialized==0x47544f53,10);
    }
    for (unsigned i=0;i<256;++i) {old[i]=src[i];dst[i]=0xa5;}
    CHECK(PremultiplyBgraRows(0,256,8,dst,256,8,2,2)==BadBuffer,11);
    CHECK(PremultiplyBgraRows(src,256,8,dst,256,8,0,2)==BadGeometry,12);
    CHECK(PremultiplyBgraRows(src,256,8,dst,256,8,UINT32_MAX,2)==BadGeometry,13);
    CHECK(PremultiplyBgraRows(src,256,7,dst,256,8,2,2)==BadStride,14);
    CHECK(PremultiplyBgraRows(src,15,8,dst,256,8,2,2)==TooShort,15);
    CHECK(PremultiplyBgraRows(src,256,8,dst,15,8,2,2)==TooShort,16);
    CHECK(PremultiplyBgraRows(src,256,8,src+1,255,8,2,2)==Overlap,17);
    CHECK(PremultiplyBgraRows(src,256,UINT32_MAX,dst,256,8,2,UINT32_MAX)==TooShort,18);
    CHECK(PremultiplyBgraRows((const uint8_t*)(UINTPTR_MAX-2),4,4,dst,256,4,1,1)==PointerOverflow,19);
    for (unsigned i=0;i<256;++i) CHECK(src[i]==old[i] && dst[i]==0xa5,20);
    CHECK(cursor==2000 && initialized==0x47544f53,21);
    CHECK(call(0x4701,(unsigned)pass,sizeof(pass)-1)==(int)sizeof(pass)-1,22);
    return 0;
}

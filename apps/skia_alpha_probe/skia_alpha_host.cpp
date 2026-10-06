#include "skia_pixel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" void gtos_skia_assert_failure(void) { std::abort(); }
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1; } } while (false)
using namespace gtos_skia;
int main() {
    for (unsigned a=0;a<256;++a) for (unsigned c=0;c<256;++c)
        CHECK(SkMulDiv255Round(c,a)==(c*a+127)/255);
    uint8_t src[256],old[256],dst[256];uint32_t random=0x47544f53;
    for (unsigned trial=0;trial<2000;++trial) {
        random=random*1664525U+1013904223U;const uint32_t w=1+random%7;
        random=random*1664525U+1013904223U;const uint32_t h=1+random%5;
        const uint32_t ss=w*4+trial%9,ds=w*4+(trial*3)%9;
        for (unsigned i=0;i<256;++i) {random=random*1664525U+1013904223U;src[i]=(uint8_t)(random>>24);}
        const uint8_t alphas[]={0,1,7,127,128,254,255,33};
        for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x)
            if (trial<8) src[y*ss+x*4+3]=alphas[trial];
        std::memcpy(old,src,sizeof(src));std::memset(dst,0xa5,sizeof(dst));
        CHECK(PremultiplyBgraRows(src,sizeof(src),ss,dst,sizeof(dst),ds,w,h)==PixelOk);
        CHECK(std::memcmp(src,old,sizeof(src))==0);
        for (unsigned i=0;i<256;++i) {
            const uint32_t y=i/ds,x=i%ds;
            if (y>=h || x>=w*4) { CHECK(dst[i]==0xa5);continue; }
            const uint32_t at=y*ss+(x/4)*4;const unsigned channel=x%4;
            CHECK(dst[i]==(channel==3 ? src[at+3] : (src[at+2-channel]*src[at+3]+127)/255));
        }
    }
    std::memset(dst,0xa5,sizeof(dst));std::memcpy(old,src,sizeof(src));
    CHECK(PremultiplyBgraRows(nullptr,256,8,dst,256,8,2,2)==BadBuffer);
    CHECK(PremultiplyBgraRows(src,256,8,dst,256,8,0,2)==BadGeometry);
    CHECK(PremultiplyBgraRows(src,256,8,dst,256,8,UINT32_MAX,2)==BadGeometry);
    CHECK(PremultiplyBgraRows(src,256,7,dst,256,8,2,2)==BadStride);
    CHECK(PremultiplyBgraRows(src,15,8,dst,256,8,2,2)==TooShort);
    CHECK(PremultiplyBgraRows(src,256,8,dst,15,8,2,2)==TooShort);
    CHECK(PremultiplyBgraRows(src,256,8,src+1,255,8,2,2)==Overlap);
    CHECK(PremultiplyBgraRows(src,256,UINT32_MAX,dst,256,8,2,UINT32_MAX)==TooShort);
    CHECK(PremultiplyBgraRows((const uint8_t*)(UINTPTR_MAX-2),4,4,dst,256,4,1,1)==PointerOverflow);
    CHECK(std::memcmp(old,src,sizeof(src))==0);
    for (unsigned i=0;i<256;++i) CHECK(dst[i]==0xa5);
    std::puts("HOST SKIA ALPHA PASS 65536 PAIRS 2000 PIXEL CASES BOUNDS");
    return 0;
}

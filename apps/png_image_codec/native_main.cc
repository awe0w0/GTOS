#include "png_pixel.h"
#include "png_fixtures.h"
#include <stddef.h>
extern "C" void* memset(void*,int,size_t);
#if !defined(__i386__) || defined(__linux__) || defined(_WIN32)
#error Existing GTOS i386 ABI1 application required
#endif
static_assert(sizeof(void*)==4,"GTOS i386 ABI1");
static int call(unsigned n,unsigned a,unsigned b) {
    int result;asm volatile("int $0x80":"=a"(result):"a"(n),"b"(a),"c"(b):"memory","cc");return result;
}
extern "C" void gtos_skia_assert_failure(void) {
    call(0x4704,0x504effff,0);asm volatile("ud2");__builtin_unreachable();
}
alignas(8) static uint8_t context[65536];
static uint8_t work[4096],bgra[1024],rgba[1024];
static volatile unsigned cursor,initialized=0x47544f53;
#define CHECK(c,id) do { if (!(c)) return 0x10000000U|(cursor<<8)|(id); } while(false)
static bool untouched(const uint8_t* p,unsigned n,uint8_t value) {
    for(unsigned i=0;i<n;++i) if(p[i]!=value) return false;
    return true;
}
extern "C" int png_guest_main(void) {
    static const char begin[]="GTOS PNG PIXELS START REAL WUFFS SKIA ABI1\n";
    static const char pass[]="GTOS PNG PIXELS PASS FIXTURES PIXELS PREFIXES BOUNDS ABI1\n";
    CHECK(call(0x4700,0,0)==1 && !cursor && initialized==0x47544f53,1);
    CHECK(untouched(context,sizeof(context),0),2);
    CHECK(gtos_png_context_bytes()<=sizeof(context),3);
    CHECK(call(0x4701,(unsigned)begin,sizeof(begin)-1)==(int)sizeof(begin)-1,4);
    for(cursor=0;cursor<GTOS_PNG_FIXTURE_COUNT;++cursor) {
        const gtos_png_fixture& f=gtos_png_fixtures[cursor];
        gtos_png_requirements q;
        memset(&q,0x5a,sizeof(q));memset(bgra,0xa5,sizeof(bgra));memset(rgba,0xa5,sizeof(rgba));
        const uint32_t bs=f.classification==GTOS_PNG_CONFORMING?f.width*4+3:128;
        const uint32_t rs=f.classification==GTOS_PNG_CONFORMING?f.width*4+7:128;
        const gtos_png_status status=gtos_png_decode_rgba(context,sizeof(context),f.png,f.png_bytes,
            work,sizeof(work),bgra,sizeof(bgra),bs,rgba,sizeof(rgba),rs,&q);
        if(f.classification==GTOS_PNG_CONFORMING) {
            CHECK(status==GTOS_PNG_OK,5);
            CHECK(q.width==f.width && q.height==f.height && q.row_bytes==f.width*4,6);
            for(unsigned i=0;i<sizeof(rgba);++i) {
                const unsigned y=i/rs,x=i%rs;
                CHECK(rgba[i]==(y<f.height && x<f.width*4?f.expected_premul_rgba[y*f.width*4+x]:0xa5),7);
            }
            for(unsigned i=0;i<sizeof(bgra);++i) {
                const unsigned y=i/bs,x=i%bs;
                CHECK(bgra[i]==(y<f.height && x<f.width*4?f.expected_bgra[y*f.width*4+x]:0xa5),8);
            }
            for(unsigned n=0;n<f.png_bytes;++n) {
                memset(&q,0x5a,sizeof(q));memset(rgba,0xa5,sizeof(rgba));
                CHECK(gtos_png_decode_rgba(context,sizeof(context),f.png,n,work,sizeof(work),
                    bgra,sizeof(bgra),bs,rgba,sizeof(rgba),rs,&q)!=GTOS_PNG_OK,9);
                CHECK(untouched(rgba,sizeof(rgba),0xa5) && untouched((const uint8_t*)&q,sizeof(q),0x5a),10);
                if(!(n%32)) CHECK(call(0x4703,0,0)==0,11);
            }
        } else {
            CHECK(status==(f.classification==GTOS_PNG_REQUIRES_EXTENSION?GTOS_PNG_UNSUPPORTED:GTOS_PNG_CORRUPT),12);
            CHECK(untouched(rgba,sizeof(rgba),0xa5) && untouched((const uint8_t*)&q,sizeof(q),0x5a),13);
        }
        CHECK(call(0x4703,0,0)==0 && initialized==0x47544f53,14);
    }
    CHECK(call(0x4701,(unsigned)pass,sizeof(pass)-1)==(int)sizeof(pass)-1,15);
    return 0;
}

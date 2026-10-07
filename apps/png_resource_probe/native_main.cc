#include <process/abi.h>
#include <process/resource_abi.h>
#include "png_pixel.h"
#include "resource_oracles.h"
#include <stddef.h>
extern "C" void* memset(void*,int,size_t);
extern "C" int resource_probe_registers(unsigned,unsigned,unsigned);
// Linker-derived exclusive end of this process's RW pages. This diagnostic
// symbol describes our own ELF and never appears in the resource ABI.
extern "C" unsigned char resource_probe_rw_limit;
#if !defined(__i386__) || defined(__linux__) || defined(_WIN32)
#error A real freestanding GTOS i386 application is required
#endif
static_assert(sizeof(void*)==4,"GTOS i386 ABI1");
static_assert(sizeof(GtosResourceInfo)==16,"resource INFO v1");
static_assert(sizeof(GtosResourceReadRequest)==20,"resource READ v1");
alignas(8) static unsigned char context[65536];
static unsigned char input[2048],work[4096],bgra[1024],rgba[1024];
alignas(4096) static unsigned char boundary[8192];
static volatile unsigned initialized=0x47544f53U;
static int call(unsigned n,unsigned a=0,unsigned b=0) {
    int result;
    asm volatile("int $0x80":"=a"(result):"a"(n),"b"(a),"c"(b):"memory","cc");
    return result;
}
extern "C" void gtos_skia_assert_failure(void) {
    call(GTOS_SYS_EXIT,0x5253ffffU);asm volatile("ud2");__builtin_unreachable();
}
#define CHECK(c,id) do { if (!(c)) return 0x52000000U|(id); } while(false)
static bool equal(const unsigned char* a,const unsigned char* b,unsigned n) {
    for(unsigned i=0;i<n;++i) if(a[i]!=b[i]) return false;
    return true;
}
static bool untouched(const unsigned char* p,unsigned n,unsigned char v) {
    for(unsigned i=0;i<n;++i) if(p[i]!=v) return false;
    return true;
}
static uint32_t crc32(const unsigned char* p,unsigned n) {
    uint32_t crc=0xffffffffU;
    for(unsigned i=0;i<n;++i) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320U&-(crc&1));
    }
    return ~crc;
}
static GtosResourceReadRequest request(unsigned offset,unsigned destination,unsigned length) {
    GtosResourceReadRequest q={GTOS_RESOURCE_ABI_VERSION,GTOS_RESOURCE_PUBLIC_PNG_ID,
                              offset,destination,length};
    return q;
}
static int read(GtosResourceReadRequest* q,unsigned bytes=sizeof(GtosResourceReadRequest)) {
    return call(GTOS_SYS_RESOURCE_READ,(unsigned)q,bytes);
}
static int rejected_requests() {
    memset(input,0xa5,sizeof(input));
    GtosResourceReadRequest q=request(0,(unsigned)input,16);
    CHECK(read(&q,19)==GTOS_RESOURCE_ERR_BAD_SIZE,101);
    CHECK(read(&q,21)==GTOS_RESOURCE_ERR_BAD_SIZE,102);
    CHECK(call(GTOS_SYS_RESOURCE_READ,0,0)==GTOS_RESOURCE_ERR_BAD_SIZE,103);
    CHECK(call(GTOS_SYS_RESOURCE_READ,0,sizeof(q))==GTOS_RESOURCE_ERR_BAD_ADDRESS,104);
    CHECK(call(GTOS_SYS_RESOURCE_READ,0x100000U,sizeof(q))==GTOS_RESOURCE_ERR_BAD_ADDRESS,105);
    const unsigned limit=(unsigned)&resource_probe_rw_limit;
    CHECK(call(GTOS_SYS_RESOURCE_READ,limit,sizeof(q))==GTOS_RESOURCE_ERR_BAD_ADDRESS,106);
    CHECK(call(GTOS_SYS_RESOURCE_READ,limit-8,sizeof(q))==GTOS_RESOURCE_ERR_BAD_ADDRESS,107);
    q.version=2;q.id=0;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_UNSUPPORTED_VERSION,108);
    q.version=GTOS_RESOURCE_ABI_VERSION;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_NOT_FOUND,109);
    q.id=GTOS_RESOURCE_PUBLIC_PNG_ID;q.length=GTOS_RESOURCE_READ_LIMIT+1;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_TOO_LARGE,110);
    q.length=16;q.offset=RESOURCE_PNG_BYTES+1;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_RANGE,111);
    q.offset=0xffffffffU;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_RANGE,112);
    q=request(0,0xfffffff8U,16);
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,113);
    q=request(0,0,16);
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,114);
    q.destination=0x100000U;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,115);
    q.destination=(unsigned)&resource_expected_bgra[0];
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,116);
    q.destination=limit;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,117);
    unsigned char* prefix=(unsigned char*)(limit-8);
    unsigned char saved[8];
    for(unsigned i=0;i<8;++i) { saved[i]=prefix[i];prefix[i]=0x6d; }
    q=request(0,limit-8,16);
    const int bad_destination=read(&q);
    const bool prefix_unchanged=untouched(prefix,8,0x6d);
    for(unsigned i=0;i<8;++i) prefix[i]=saved[i];
    CHECK(bad_destination==GTOS_RESOURCE_ERR_BAD_ADDRESS && prefix_unchanged,118);
    q=request(RESOURCE_PNG_BYTES,(unsigned)input,GTOS_RESOURCE_READ_LIMIT);
    CHECK(read(&q)==0 && untouched(input,sizeof(input),0xa5),119);
    q.destination=0;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,120);
    q=request(0,limit,0);
    CHECK(read(&q)==0,121);
    CHECK(resource_probe_registers(GTOS_SYS_RESOURCE_READ,(unsigned)&q,sizeof(q))==1,132);
    q.destination=0;
    CHECK(read(&q)==GTOS_RESOURCE_ERR_BAD_ADDRESS,122);
    GtosResourceInfo info;
    memset(&info,0x5a,sizeof(info));
    CHECK(call(GTOS_SYS_RESOURCE_INFO,0,(unsigned)&info)==GTOS_RESOURCE_ERR_NOT_FOUND,123);
    CHECK(untouched((unsigned char*)&info,sizeof(info),0x5a),124);
    CHECK(call(GTOS_SYS_RESOURCE_INFO,0,0)==GTOS_RESOURCE_ERR_NOT_FOUND,125);
    CHECK(call(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,0)==GTOS_RESOURCE_ERR_BAD_ADDRESS,126);
    CHECK(call(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,0x100000U)==GTOS_RESOURCE_ERR_BAD_ADDRESS,127);
    CHECK(call(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,
               (unsigned)&resource_expected_bgra[0])==GTOS_RESOURCE_ERR_BAD_ADDRESS,128);
    CHECK(call(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,limit)==GTOS_RESOURCE_ERR_BAD_ADDRESS,129);
    for(unsigned i=0;i<8;++i) { saved[i]=prefix[i];prefix[i]=0x6d; }
    const int bad_info=call(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,limit-8);
    const bool info_unchanged=untouched(prefix,8,0x6d);
    for(unsigned i=0;i<8;++i) prefix[i]=saved[i];
    CHECK(bad_info==GTOS_RESOURCE_ERR_BAD_ADDRESS && info_unchanged,130);
    CHECK(untouched(input,sizeof(input),0xa5) && initialized==0x47544f53U,131);
    return 0;
}
extern "C" int png_guest_main(void) {
    static const char begin[]="GTOS PNG RESOURCE START V1\n";
    static const char pass[]="GTOS PNG RESOURCE PASS READ DECODE PIXELS V1\n";
    CHECK(call(GTOS_SYS_ABI)==GTOS_NATIVE_ABI_VERSION,1);
    CHECK(call(0xffffffffU)==GTOS_ERR_UNSUPPORTED,21);
    CHECK(call(GTOS_SYS_WRITE,(unsigned)begin,sizeof(begin)-1)==(int)sizeof(begin)-1,2);
    CHECK(untouched(context,sizeof(context),0) && initialized==0x47544f53U,3);
    const int rejected=rejected_requests();
    if(rejected) return rejected;
    GtosResourceInfo info;
    CHECK(resource_probe_registers(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,
                                  (unsigned)&info)==1,5);
    CHECK(info.version==GTOS_RESOURCE_ABI_VERSION && info.type==GTOS_RESOURCE_TYPE_PNG &&
          info.bytes==RESOURCE_PNG_BYTES &&
          info.flags==(GTOS_RESOURCE_FLAG_READONLY|GTOS_RESOURCE_FLAG_PUBLIC),6);
    unsigned offset=0,calls=0;
    while(offset<info.bytes) {
        GtosResourceReadRequest q=request(offset,(unsigned)(input+offset),GTOS_RESOURCE_READ_LIMIT);
        const int n=read(&q);
        const unsigned expected=info.bytes-offset<GTOS_RESOURCE_READ_LIMIT?
                                info.bytes-offset:GTOS_RESOURCE_READ_LIMIT;
        CHECK(n==(int)expected,7);
        offset+=(unsigned)n;++calls;
        CHECK(call(GTOS_SYS_YIELD)==0,8);
    }
    CHECK(calls==5 && offset==RESOURCE_PNG_BYTES &&
          crc32(input,offset)==RESOURCE_PNG_CRC32 &&
          untouched(input+offset,sizeof(input)-offset,0xa5),9);
    // Repeated partial reads are byte-identical and never expose mutable backing.
    GtosResourceReadRequest last=request(RESOURCE_PNG_BYTES-1,(unsigned)boundary,16);
    memset(boundary,0x6d,sizeof(boundary));
    CHECK(read(&last)==1 && boundary[0]==input[RESOURCE_PNG_BYTES-1] &&
          untouched(boundary+1,sizeof(boundary)-1,0x6d),10);
    last=request(0,(unsigned)boundary,GTOS_RESOURCE_READ_LIMIT);
    CHECK(read(&last)==(int)GTOS_RESOURCE_READ_LIMIT,22);
    boundary[0]^=0xff;
    CHECK(read(&last)==(int)GTOS_RESOURCE_READ_LIMIT &&
          equal(boundary,input,GTOS_RESOURCE_READ_LIMIT),23);
    // Both descriptor and destination legitimately span two mapped user pages.
    GtosResourceReadRequest* crossing=(GtosResourceReadRequest*)(boundary+4096-8);
    *crossing=request(0,(unsigned)(boundary+4096+128),64);
    CHECK(read(crossing)==64 && equal(boundary+4096+128,input,64),11);
    GtosResourceInfo* crossing_info=(GtosResourceInfo*)(boundary+4096-8);
    CHECK(call(GTOS_SYS_RESOURCE_INFO,GTOS_RESOURCE_PUBLIC_PNG_ID,(unsigned)crossing_info)==0 &&
          crossing_info->version==1 && crossing_info->bytes==RESOURCE_PNG_BYTES,12);
    GtosResourceReadRequest across=request(0,(unsigned)(boundary+4096-8),64);
    CHECK(read(&across)==64 && equal(boundary+4096-8,input,64),13);
    // Snapshot semantics allow output to overwrite part of the request itself.
    crossing=(GtosResourceReadRequest*)(boundary+4096-8);
    *crossing=request(0,(unsigned)(boundary+4096-4),64);
    CHECK(read(crossing)==64 && equal(boundary+4096-4,input,64),14);
    gtos_png_requirements needed;
    memset(&needed,0x5a,sizeof(needed));
    CHECK(gtos_png_context_bytes()<=sizeof(context) &&
          gtos_png_inspect(context,sizeof(context),input,info.bytes,&needed)==GTOS_PNG_OK,15);
    CHECK(needed.width==RESOURCE_WIDTH && needed.height==RESOURCE_HEIGHT &&
          needed.row_bytes==64 && needed.bgra_bytes==1024 && needed.work_bytes<=sizeof(work),16);
    memset(bgra,0xa5,sizeof(bgra));memset(rgba,0xa5,sizeof(rgba));
    CHECK(gtos_png_decode_rgba(context,sizeof(context),input,info.bytes,work,sizeof(work),
          bgra,sizeof(bgra),64,rgba,sizeof(rgba),64,&needed)==GTOS_PNG_OK,17);
    CHECK(equal(bgra,resource_expected_bgra,sizeof(bgra)) &&
          equal(rgba,resource_expected_premul_rgba,sizeof(rgba)),18);
    CHECK(crc32(input,info.bytes)==RESOURCE_PNG_CRC32 && initialized==0x47544f53U,19);
    CHECK(call(GTOS_SYS_WRITE,(unsigned)pass,sizeof(pass)-1)==(int)sizeof(pass)-1,20);
    return 0;
}

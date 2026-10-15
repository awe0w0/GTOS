#include <process/abi.h>
#include <stdlib.h>
#include <errno.h>
#include "heap.h"
#include "record.h"
extern "C" {
volatile HeapRecord native_heap_record __attribute__((section(".data.heap_record"))) = {1};
int posix_memalign(void**, size_t, size_t) noexcept;
}
namespace {
auto volatile Malloc = &malloc;
auto volatile Calloc = &calloc;
auto volatile Realloc = &realloc;
auto volatile Free = &free;
auto volatile Aligned = &posix_memalign;
void* blocks[160];
int Call(unsigned op, unsigned a=0, unsigned b=0) {
    asm volatile("int $0x80" : "+a"(op) : "b"(a), "c"(b) : "memory", "cc"); return (int)op;
}
void Yield() { Call(GTOS_SYS_YIELD); }
void Need(bool pass, unsigned line) {
    native_heap_record.checks=native_heap_record.checks+1;
    if (!pass) { native_heap_record.error=line; Call(GTOS_SYS_EXIT,line); for (;;) Yield(); }
}
#define NEED(x) Need((x),__LINE__)
void Fill(void* pointer,unsigned n,unsigned char value) {
    auto* p=(unsigned char*)pointer; for (unsigned i=0;i<n;++i) p[i]=value;
}
void Check(const void* pointer,unsigned n,unsigned char value) {
    auto* p=(const unsigned char*)pointer; for (unsigned i=0;i<n;++i) NEED(p[i]==value);
}
void Main() {
    errno=73; NEED(Malloc(0)==nullptr && Calloc(0,~0U)==nullptr && errno==73);
    NEED(Malloc(65536)==nullptr && errno==ENOMEM);
    void* first=Malloc(1024); NEED(first); errno=73;
    NEED(gtos_native_heap_enable_large_allocations()==EBUSY && errno==73);
    Free(first); NEED(gtos_native_heap_enable_large_allocations()==0 && errno==73);
    if (native_heap_record.mode) {
        for (unsigned i=0;i<160;++i) { blocks[i]=Malloc(1024); NEED(blocks[i]); Fill(blocks[i],1024,(unsigned char)i); }
        void* large=nullptr; NEED(Aligned(&large,4096,70000)==0 && large && !((unsigned)large&4095));
        Fill(large,70000,0x5a); native_heap_record.held=(unsigned)large; native_heap_record.stage=10;
        if (native_heap_record.mode==1) Free((unsigned char*)large+1);
        if (native_heap_record.mode==2) { Free(large); Free(large); }
        if (native_heap_record.mode==3) asm volatile("ud2");
        for (;;) Yield();
    }
    for (unsigned i=0;i<160;++i) {
        blocks[i]=Malloc(1024); NEED(blocks[i] && !((unsigned)blocks[i]&15)); Fill(blocks[i],1024,(unsigned char)i);
    }
    // The primary arena can be empty while extra arenas still have live blocks.
    GtosVmRegionInfo primary={}; NEED(gtos_native_heap_query(&primary)==1);
    for (unsigned i=0;i<160;++i) if ((unsigned)blocks[i]>=primary.base && (unsigned)blocks[i]<primary.base+primary.length) {
        Free(blocks[i]); blocks[i]=nullptr;
    }
    NEED(gtos_native_heap_query(&primary)==0 && gtos_native_heap_enable_large_allocations()==EBUSY);
    for (unsigned i=0;i<160;++i) if (blocks[i]) { Check(blocks[i],1024,(unsigned char)i); Free(blocks[i]); blocks[i]=nullptr; }
    NEED(gtos_native_heap_enable_large_allocations()==0);
    void* aligned=nullptr; NEED(Aligned(&aligned,256,257)==0 && aligned && !((unsigned)aligned&255));
    Fill(aligned,257,0x6b); void* grown=Realloc(aligned,100000); NEED(grown && !((unsigned)grown&255)); Check(grown,257,0x6b);
    Fill(grown,100000,0x7c); void* shrunk=Realloc(grown,33); NEED(shrunk==grown); Check(shrunk,33,0x7c);
    void* larger=Realloc(shrunk,200000); NEED(larger && !((unsigned)larger&255)); Check(larger,33,0x7c);
    Fill(larger,200000,0x8d); errno=73; NEED(Realloc(larger,~0U)==nullptr && errno==ENOMEM); Check(larger,200000,0x8d);
    NEED(Realloc(larger,0)==nullptr);
    errno=73; NEED(Calloc(~0U,2)==nullptr && errno==ENOMEM);
    void* zero=Calloc(70,1024); NEED(zero); Check(zero,71680,0); Free(zero);
    for (unsigned i=0;i<16;++i) { blocks[i]=nullptr; NEED(Aligned(&blocks[i],8*1024*1024,1)==0 && blocks[i] && !((unsigned)blocks[i]&0x7fffff)); Fill(blocks[i],1,(unsigned char)i); }
    void* sentinel=(void*)0x12345678U; errno=73;
    NEED(Aligned(&sentinel,8*1024*1024,1)==ENOMEM && sentinel==(void*)0x12345678U && errno==73);
    Free(blocks[3]); blocks[3]=nullptr; NEED(Aligned(&blocks[3],8*1024*1024,1)==0); Fill(blocks[3],1,3);
    for (unsigned i=0;i<16;++i) { Check(blocks[i],1,(unsigned char)i); Free(blocks[i]); blocks[i]=nullptr; }
    // 256 pages cannot fit alongside this image/stack in the default process.
    // Repetition detects a leaked reservation on failed commit.
    for (unsigned i=0;i<40;++i) { errno=73; NEED(Malloc(1024*1024)==nullptr && errno==ENOMEM); }
    first=Malloc(128); NEED(first); Fill(first,128,0x9e); Check(first,128,0x9e); Free(first);
    sentinel=(void*)0x12345678U; errno=73;
    NEED(Aligned(&sentinel,3,40)==EINVAL && sentinel==(void*)0x12345678U && errno==73);
    NEED(Aligned(&sentinel,64,0)==0 && !sentinel && errno==73);
    native_heap_record.stage=20; Call(GTOS_SYS_EXIT,0); for (;;) Yield();
}
}
extern "C" __attribute__((section(".text.entry"),noreturn)) void _start() { Main(); for (;;) Yield(); }

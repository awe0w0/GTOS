#include "probe.h"
#include <process/abi.h>
#include <errno.h>

#ifndef GTOS_HEAP_PROBE_MODE
#define GTOS_HEAP_PROBE_MODE 0
#endif
extern "C" {
    volatile HeapProbeRecord heap_probe_record __attribute__((section(".data.heap_record"),used))={1,GTOS_HEAP_PROBE_MODE,0,0,0,0,0,0,0,0};
}
namespace {
    static const char start[]="GTOS V8 HEAP START V1\n";
    [[maybe_unused]] static const char pass[]="GTOS V8 HEAP PASS MEMORY HELPERS V1\n";
    static const char failure[]="GTOS V8 HEAP CHECK FAIL V1\n";
    volatile size_t impossible_bytes=~size_t(0);
    unsigned Fail(unsigned line) {
        heap_probe_call(GTOS_SYS_WRITE,(unsigned)failure,sizeof(failure)-1);
        return 0x48010000U|line;
    }
    void Pattern(void* pointer,unsigned bytes,unsigned mask) {
        volatile unsigned char* out=(volatile unsigned char*)pointer;
        for (unsigned i=0;i<bytes;++i) out[i]=(unsigned char)(i^mask);
    }
    bool Matches(const void* pointer,unsigned bytes,unsigned mask) {
        const volatile unsigned char* in=(const volatile unsigned char*)pointer;
        for (unsigned i=0;i<bytes;++i) if (in[i]!=(unsigned char)(i^mask)) return false;
        return true;
    }
    bool Zero(const void* pointer,unsigned bytes) {
        const volatile unsigned char* in=(const volatile unsigned char*)pointer;
        for (unsigned i=0;i<bytes;++i) if (in[i]) return false;
        return true;
    }
    bool Empty() {
        GtosVmRegionInfo info={0xA5A5A5A5U,0xA5A5A5A5U,0xA5A5A5A5U,0xA5A5A5A5U,0xA5A5A5A5U};
        return !gtos_native_heap_query(&info) && info.version==0xA5A5A5A5U && info.handle==0xA5A5A5A5U
            && info.base==0xA5A5A5A5U && info.length==0xA5A5A5A5U && info.resident_pages==0xA5A5A5A5U;
    }
    bool Live() {
        GtosVmRegionInfo info;
        return gtos_native_heap_query(&info)==1 && info.version==1 && info.handle && info.handle<=0x7FFFFFFFU
            && info.base>=0x80000000U && info.base<=0xBFFFC000U-GTOS_NATIVE_HEAP_BYTES
            && info.length==GTOS_NATIVE_HEAP_BYTES && info.resident_pages==16;
    }
}
#define CHECK(expression) do { if (!(expression)) return Fail(__LINE__); heap_probe_record.checks=heap_probe_record.checks+1; } while (false)
#define CPP_PAIR(allocation,deallocation,alignment) do { \
    void* object=actual_cpp_new_##allocation(256); \
    CHECK(object && !((unsigned)object&((alignment)-1))); \
    CHECK(Live()); Pattern(object,256,0x5BU); CHECK(Matches(object,256,0x5BU)); \
    actual_cpp_delete_##deallocation(object,256); CHECK(Empty()); \
} while (false)
#define CPP_ZERO(name,alignment) do { \
    void* object=actual_cpp_new_##name(0); CHECK(object && !((unsigned)object&((alignment)-1))); \
    *(volatile unsigned char*)object=0x5A; CHECK(*(volatile unsigned char*)object==0x5A); \
    actual_cpp_delete_##name(object,1); CHECK(Empty()); \
} while (false)
extern "C" unsigned heap_guest_main() {
    CHECK(heap_probe_call(GTOS_SYS_ABI,0,0)==1);
    CHECK(heap_probe_call(GTOS_SYS_WRITE,(unsigned)start,sizeof(start)-1)==sizeof(start)-1);
    CHECK(Empty()); errno=17;
    CHECK(!actual_v8_malloc(0) && errno==17 && Empty());
    CHECK(!actual_v8_calloc(0,4096) && errno==17 && Empty());
    CHECK(!actual_v8_calloc(4096,0) && errno==17 && Empty());
    actual_v8_free(nullptr); CHECK(Empty());
    CHECK(!actual_v8_malloc(impossible_bytes) && errno==ENOMEM && Empty());
    errno=19; CHECK(!actual_v8_calloc(impossible_bytes,2) && errno==ENOMEM && Empty());
    errno=23; CHECK(!actual_v8_aligned_alloc(512,3) && errno==23 && Empty());

    void* primary=actual_v8_malloc(4096); CHECK(primary && !((unsigned)primary&15)); CHECK(Live());
    Pattern(primary,4096,0x21);
    void* neighbor=actual_v8_calloc(1,4096); CHECK(neighbor && neighbor!=primary && Zero(neighbor,4096));
    Pattern(neighbor,4096,0x93);
    void* moved=actual_v8_realloc(primary,8192); CHECK(moved && moved!=neighbor);
    CHECK(Matches(moved,4096,0x21) && Matches(neighbor,4096,0x93));
    moved=actual_v8_realloc(moved,128); CHECK(moved && Matches(moved,128,0x21));
    errno=31; CHECK(!actual_v8_realloc(moved,impossible_bytes) && errno==ENOMEM);
    CHECK(Matches(moved,128,0x21) && Matches(neighbor,4096,0x93) && Live());
    actual_v8_free(moved); CHECK(Matches(neighbor,4096,0x93));
    actual_v8_free(neighbor); CHECK(Empty());
    primary=actual_v8_realloc(nullptr,64); CHECK(primary && Live());
    Pattern(primary,64,0x19); CHECK(Matches(primary,64,0x19));
    actual_v8_free(primary); CHECK(Empty());
    primary=actual_v8_aligned_alloc(511,64); CHECK(primary && !((unsigned)primary&63));
    Pattern(primary,511,0x66); CHECK(Matches(primary,511,0x66));
    actual_v8_aligned_free(primary); CHECK(Empty());
    primary=actual_v8_aligned_alloc(4355,4096); CHECK(primary && !((unsigned)primary&4095));
    Pattern(primary,4355,0x7B); CHECK(Matches(primary,4355,0x7B));
    actual_v8_aligned_free(primary); CHECK(Empty());
    size_t count=0; primary=actual_v8_allocate_at_least(157,&count);
    CHECK(primary && count==157); Pattern(primary,157,0x48); CHECK(Matches(primary,157,0x48));
    actual_v8_free(primary); CHECK(Empty());

    CPP_PAIR(scalar,scalar,16); CPP_PAIR(array,array,16);
    CPP_PAIR(nothrow_scalar,nothrow_scalar,16); CPP_PAIR(nothrow_array,nothrow_array,16);
    CPP_PAIR(scalar,sized_scalar,16); CPP_PAIR(array,sized_array,16);
    CPP_PAIR(aligned_scalar,aligned_scalar,64); CPP_PAIR(aligned_array,aligned_array,64);
    CPP_PAIR(aligned_nothrow_scalar,aligned_nothrow_scalar,64); CPP_PAIR(aligned_nothrow_array,aligned_nothrow_array,64);
    CPP_PAIR(aligned_scalar,sized_aligned_scalar,64); CPP_PAIR(aligned_array,sized_aligned_array,64);
    CPP_ZERO(scalar,16); CPP_ZERO(array,16);
    CPP_ZERO(nothrow_scalar,16); CPP_ZERO(nothrow_array,16);
    CPP_ZERO(aligned_scalar,64); CPP_ZERO(aligned_array,64);
    CPP_ZERO(aligned_nothrow_scalar,64); CPP_ZERO(aligned_nothrow_array,64);
    CHECK(!actual_cpp_new_nothrow_scalar(impossible_bytes) && Empty());
    CHECK(!actual_cpp_new_nothrow_array(impossible_bytes) && Empty());
    CHECK(!actual_cpp_new_aligned_nothrow_scalar(impossible_bytes) && Empty());
    CHECK(!actual_cpp_new_aligned_nothrow_array(impossible_bytes) && Empty());
    primary=actual_cpp_new_scalar(0); neighbor=actual_cpp_new_nothrow_array(0);
    CHECK(primary && neighbor && primary!=neighbor);
    actual_cpp_delete_scalar(primary,1); actual_cpp_delete_nothrow_array(neighbor,1); CHECK(Empty());
    CHECK(actual_cpp_constructed()==0 && actual_cpp_destroyed()==0);
    CHECK(!actual_cpp_construct_nothrow_oversized() && actual_cpp_constructed()==0 && actual_cpp_destroyed()==0 && Empty());
    primary=actual_cpp_construct(); CHECK(primary && !((unsigned)primary&63));
    CHECK(*(volatile unsigned*)primary==0x43505048U && actual_cpp_constructed()==1 && actual_cpp_destroyed()==0);
    actual_cpp_destroy(primary); CHECK(actual_cpp_constructed()==1 && actual_cpp_destroyed()==1 && Empty());

    primary=actual_v8_malloc(4096); neighbor=actual_v8_malloc(4096);
    CHECK(primary && neighbor && primary!=neighbor && Live());
    Pattern(primary,4096,0x37); Pattern(neighbor,4096,0xA9);
    GtosVmRegionInfo info; CHECK(gtos_native_heap_query(&info)==1);
    heap_probe_record.base=info.base; heap_probe_record.handle=info.handle;
    heap_probe_record.primary=(unsigned)primary; heap_probe_record.primaryBytes=4096;
    heap_probe_record.neighbor=(unsigned)neighbor; heap_probe_record.neighborBytes=4096;
#if GTOS_HEAP_PROBE_MODE==3 || GTOS_HEAP_PROBE_MODE==4
    actual_v8_free(primary); actual_v8_free(neighbor); CHECK(Empty());
    heap_probe_record.primary=0; heap_probe_record.primaryBytes=0;
    heap_probe_record.neighbor=0; heap_probe_record.neighborBytes=0;
#if GTOS_HEAP_PROBE_MODE==4
    primary=actual_v8_malloc(4096); CHECK(primary && Zero(primary,4096));
    GtosVmRegionInfo replacement; CHECK(gtos_native_heap_query(&replacement)==1 && replacement.handle>info.handle);
    heap_probe_record.base=replacement.base; heap_probe_record.handle=replacement.handle;
    heap_probe_record.primary=(unsigned)primary; heap_probe_record.primaryBytes=4096;
#endif
#endif
#if GTOS_HEAP_PROBE_MODE==1
    heap_probe_record.stage=1;
    *(volatile unsigned*)0xBFFFCFFCU=0x554E4D50U;
    return Fail(__LINE__);
#elif GTOS_HEAP_PROBE_MODE==2
    heap_probe_record.stage=1;
    for (;;) heap_probe_call(GTOS_SYS_YIELD,0,0);
#elif GTOS_HEAP_PROBE_MODE==5
    heap_probe_record.stage=1;
    (void)actual_cpp_new_array(impossible_bytes);
    return Fail(__LINE__);
#else
    heap_probe_record.stage=2;
    CHECK(heap_probe_call(GTOS_SYS_WRITE,(unsigned)pass,sizeof(pass)-1)==sizeof(pass)-1);
    return 0;
#endif
}

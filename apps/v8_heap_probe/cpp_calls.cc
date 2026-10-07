#include "probe.h"
#include <new>
extern "C" [[gnu::noinline]] void* actual_cpp_new_scalar(size_t bytes) { return ::operator new(bytes); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_array(size_t bytes) { return ::operator new[](bytes); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_nothrow_scalar(size_t bytes) { return ::operator new(bytes,std::nothrow); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_nothrow_array(size_t bytes) { return ::operator new[](bytes,std::nothrow); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_aligned_scalar(size_t bytes) { return ::operator new(bytes,std::align_val_t(64)); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_aligned_array(size_t bytes) { return ::operator new[](bytes,std::align_val_t(64)); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_aligned_nothrow_scalar(size_t bytes) { return ::operator new(bytes,std::align_val_t(64),std::nothrow); }
extern "C" [[gnu::noinline]] void* actual_cpp_new_aligned_nothrow_array(size_t bytes) { return ::operator new[](bytes,std::align_val_t(64),std::nothrow); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_scalar(void* p,size_t) { ::operator delete(p); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_array(void* p,size_t) { ::operator delete[](p); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_nothrow_scalar(void* p,size_t) { ::operator delete(p,std::nothrow); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_nothrow_array(void* p,size_t) { ::operator delete[](p,std::nothrow); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_sized_scalar(void* p,size_t bytes) { ::operator delete(p,bytes); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_sized_array(void* p,size_t bytes) { ::operator delete[](p,bytes); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_aligned_scalar(void* p,size_t) { ::operator delete(p,std::align_val_t(64)); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_aligned_array(void* p,size_t) { ::operator delete[](p,std::align_val_t(64)); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_aligned_nothrow_scalar(void* p,size_t) { ::operator delete(p,std::align_val_t(64),std::nothrow); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_aligned_nothrow_array(void* p,size_t) { ::operator delete[](p,std::align_val_t(64),std::nothrow); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_sized_aligned_scalar(void* p,size_t bytes) { ::operator delete(p,bytes,std::align_val_t(64)); }
extern "C" [[gnu::noinline]] void actual_cpp_delete_sized_aligned_array(void* p,size_t bytes) { ::operator delete[](p,bytes,std::align_val_t(64)); }
namespace {
    unsigned constructed,destroyed;
    struct alignas(64) Oversized {
        unsigned char payload[GTOS_NATIVE_HEAP_BYTES+64];
        Oversized() noexcept { ++constructed; }
    };
    static_assert(sizeof(Oversized)>GTOS_NATIVE_HEAP_BYTES,"Oversized nothrow object exceeds the arena");
    struct alignas(64) Tracked {
        unsigned tag;
        Tracked() noexcept:tag(0x43505048U) { ++constructed; }
        ~Tracked() { ++destroyed; }
    };
}
extern "C" [[gnu::noinline]] void* actual_cpp_construct() { return new Tracked; }
extern "C" [[gnu::noinline]] void* actual_cpp_construct_nothrow_oversized() { return new(std::nothrow) Oversized; }
extern "C" [[gnu::noinline]] void actual_cpp_destroy(void* p) { delete static_cast<Tracked*>(p); }
extern "C" unsigned actual_cpp_constructed() { return constructed; }
extern "C" unsigned actual_cpp_destroyed() { return destroyed; }

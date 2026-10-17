#include "environment.h"
#include "config/app.h"
#include "src/stdlib/environ_internal.h"
static_assert(sizeof(uintptr_t) == 4 && __atomic_always_lock_free(4,0), "Native environment requires lock-free i386 words");

namespace {
    GtosEnvironmentReadResult startup;
    uintptr_t pointers[GTOS_ENVIRONMENT_MAXIMUM_ENTRIES+1];
    unsigned initialization;
    void Clear() {
        unsigned char* bytes = (unsigned char*)&startup;
        for (unsigned i = 0; i < sizeof(startup); ++i) bytes[i] = 0;
        for (unsigned i = 0; i < GTOS_ENVIRONMENT_MAXIMUM_ENTRIES+1; ++i) pointers[i] = 0;
    }
}
namespace gtos_native {
    bool ValidEnvironment(const GtosEnvironmentReadResult& value) {
        if (value.version != GTOS_ENVIRONMENT_ABI_VERSION || value.reserved
            || value.count > GTOS_ENVIRONMENT_MAXIMUM_ENTRIES
            || value.data_bytes > GTOS_ENVIRONMENT_DATA_BYTES) return false;
        unsigned at = 0;
        for (unsigned i = 0; i < value.count; ++i) {
            if (value.offsets[i] != at) return false;
            const unsigned begin = at;
            unsigned equal = value.data_bytes;
            while (at < value.data_bytes && value.data[at]) {
                if (equal == value.data_bytes && value.data[at] == '=') equal = at;
                ++at;
            }
            if (at == value.data_bytes || equal == value.data_bytes || equal == begin) return false;
            for (unsigned old = 0; old < i; ++old) {
                unsigned previous = value.offsets[old], current = begin;
                while (current < equal && value.data[previous] == value.data[current]) { ++previous; ++current; }
                if (current == equal && value.data[previous] == '=') return false;
            }
            ++at;
        }
        if (at != value.data_bytes) return false;
        for (unsigned i = value.count; i < GTOS_ENVIRONMENT_MAXIMUM_ENTRIES; ++i)
            if (value.offsets[i]) return false;
        for (unsigned i = value.data_bytes; i < GTOS_ENVIRONMENT_DATA_BYTES; ++i)
            if (value.data[i]) return false;
        return true;
    }
}
extern "C" int gtos_native_environment_initialize() noexcept {
    if (__atomic_load_n(&initialization,__ATOMIC_ACQUIRE) == 2) return 0;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&initialization,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
        return expected == 2 ? 0 : -16;
    const GtosEnvironmentReadRequest request = {GTOS_ENVIRONMENT_ABI_VERSION,0,(unsigned)&startup,sizeof(startup)};
    int result;
    asm volatile("int $0x80" : "=a"(result) : "0"(GTOS_SYS_ENVIRONMENT_READ), "b"((unsigned)&request),
                 "c"(sizeof(request)) : "memory", "cc");
    if (result || !gtos_native::ValidEnvironment(startup)) {
        Clear(); __atomic_store_n(&initialization,0,__ATOMIC_RELEASE);
        return result < 0 ? result : GTOS_ENVIRONMENT_ERR_BAD_SIZE;
    }
    for (unsigned i = 0; i < startup.count; ++i) pointers[i] = (uintptr_t)(startup.data+startup.offsets[i]);
    pointers[startup.count] = 0;
    LIBC_NAMESPACE::app.env_ptr = pointers;
    LIBC_NAMESPACE::internal::EnvironmentManager::get_instance();
    __atomic_store_n(&initialization,2,__ATOMIC_RELEASE);
    return 0;
}

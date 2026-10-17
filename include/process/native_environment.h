#ifndef GTOS_PROCESS_NATIVE_ENVIRONMENT_H
#define GTOS_PROCESS_NATIVE_ENVIRONMENT_H
#include <process/environment_abi.h>
namespace gtos { namespace process {
    class NativeEnvironment {
        GtosEnvironmentReadResult value;
    public:
        NativeEnvironment() { Clear(); }
        void Clear() {
            unsigned char* bytes = (unsigned char*)&value;
            for (unsigned i = 0; i < sizeof(value); ++i) bytes[i] = 0;
            value.version = GTOS_ENVIRONMENT_ABI_VERSION;
        }
        // Kernel launch input only, not a user address. The caller keeps this
        // bounded buffer readable for the call; no pointer is retained.
        bool Assign(const char* data, unsigned bytes) {
            if (bytes > GTOS_ENVIRONMENT_DATA_BYTES || (bytes && !data)) return false;
            unsigned offsets[GTOS_ENVIRONMENT_MAXIMUM_ENTRIES], count = 0, at = 0;
            while (at < bytes) {
                if (count == GTOS_ENVIRONMENT_MAXIMUM_ENTRIES) return false;
                const unsigned begin = at;
                unsigned equal = bytes;
                while (at < bytes && data[at]) {
                    if (equal == bytes && data[at] == '=') equal = at;
                    ++at;
                }
                if (at == bytes || equal == bytes || equal == begin) return false;
                for (unsigned i = 0; i < count; ++i) {
                    unsigned old = offsets[i], current = begin;
                    while (current < equal && data[old] == data[current]) { ++old; ++current; }
                    if (current == equal && data[old] == '=') return false;
                }
                offsets[count++] = begin; ++at;
            }
            // A caller may reuse this object's own data as input.
            char copy[GTOS_ENVIRONMENT_DATA_BYTES];
            for (unsigned i = 0; i < bytes; ++i) copy[i] = data[i];
            // Validation is complete before touching the previous value.
            Clear(); value.count = count; value.data_bytes = bytes;
            for (unsigned i = 0; i < count; ++i) value.offsets[i] = offsets[i];
            for (unsigned i = 0; i < bytes; ++i) value.data[i] = copy[i];
            return true;
        }
        const GtosEnvironmentReadResult& Snapshot() const { return value; }
    };
} }
#endif

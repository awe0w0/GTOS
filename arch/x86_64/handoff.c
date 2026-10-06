#include "handoff.h"
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t u64(const unsigned char *p) { return u32(p) | (uint64_t)u32(p+4)<<32; }
const char *handoff_validate(const void *bytes, size_t available,
                            uint64_t kernel_start, uint64_t kernel_end,
                            struct handoff_summary *out) {
    const unsigned char *p = bytes;
    struct handoff_summary result = {0, 0};
    if (!p || !out || available < 16) return "short header";
    uint32_t total = u32(p);
    if (total < 16 || total > BOOTINFO_LIMIT || total > available || (total & 7)) return "total size";
    if (kernel_start >= kernel_end) return "kernel range";
    uint32_t off = 8;
    int mmap_seen = 0, kernel_usable = 0;
    while (off < total) {
        if (total-off < 8) return "short tag";
        uint32_t type = u32(p+off), size = u32(p+off+4);
        if (size < 8 || size > total-off) return "tag size";
        if (type == 0) {
            if (size != 8 || off != total-8) return "end tag";
            if (!mmap_seen || !kernel_usable) return "usable kernel missing";
            *out = result;
            return 0;
        }
        if (type == 6) {
            if (mmap_seen++ || size < 16) return "memory map header";
            uint32_t stride = u32(p+off+8);
            if (stride < 24 || (stride & 7) || u32(p+off+12) || (size-16)%stride) return "memory map stride";
            for (uint32_t e=16; e<size; e+=stride) {
                const unsigned char *entry = p+off+e;
                uint64_t start = u64(entry), length = u64(entry+8);
                if (!length || length > UINT64_MAX-start) return "memory map range";
                uint64_t end = start+length;
                uint32_t kind = u32(entry+16);
                /* This bounded target refuses ambiguous or overlapping maps. */
                for (uint32_t prev=16; prev<e; prev+=stride) {
                    uint64_t a = u64(p+off+prev), b = a+u64(p+off+prev+8);
                    if (start < b && a < end) return "overlapping memory map";
                }
                if (kind == 1) {
                    if (length > UINT64_MAX-result.usable_bytes) return "memory size overflow";
                    result.usable_bytes += length;
                    if (start <= kernel_start && end >= kernel_end) kernel_usable = 1;
                }
                ++result.mmap_entries;
            }
        }
        uint32_t padded = (size+7u)&~7u; /* size <= 65536, cannot wrap */
        if (padded > total-off) return "tag padding";
        off += padded;
    }
    return "missing end tag";
}

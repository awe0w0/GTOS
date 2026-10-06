#ifndef GTOS_X64_HANDOFF_H
#define GTOS_X64_HANDOFF_H
#include <stdint.h>
#include <stddef.h>
#define BOOTINFO_LIMIT 65536u
#define BOOTINFO_CEILING 0x04000000u
struct handoff_summary { uint64_t usable_bytes; uint32_t mmap_entries; };
struct handoff_range { uint64_t start, end; };
struct handoff_memory_range { uint64_t start, end; uint32_t type; };
/* Trusted read-only view: created only by handoff_parse, and valid only while
 * its private resident input remains immutable. It contains no original tag
 * pointers and never authorizes dereferencing a described physical range. */
struct handoff_view {
    const unsigned char *bytes;
    uint64_t usable_bytes;
    uint32_t total_size, mmap_offset, mmap_stride, mmap_entries;
    uint32_t module_count, framebuffer_offset;
};
/* Never follows pointers from tags. Caller supplies accessible immutable bytes.
 * Neither function publishes any output before the entire copy is validated. */
const char *handoff_validate(const void *bytes, size_t available,
                            uint64_t kernel_start, uint64_t kernel_end,
                            struct handoff_summary *out);
const char *handoff_parse(const void *bytes, size_t available,
                         uint64_t kernel_start, uint64_t kernel_end,
                         struct handoff_view *out);
/* Internal consumers must use an unmodified successful view. These return zero
 * at end; initialize the exclusion cursor to zero. They do not follow pointers. */
int handoff_memory_range(const struct handoff_view *, uint32_t index,
                         struct handoff_memory_range *out);
int handoff_next_exclusion(const struct handoff_view *, uint32_t *cursor,
                           struct handoff_range *out);
#endif

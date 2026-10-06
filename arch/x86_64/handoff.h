#ifndef GTOS_X64_HANDOFF_H
#define GTOS_X64_HANDOFF_H
#include <stdint.h>
#include <stddef.h>
#define BOOTINFO_LIMIT 65536u
#define BOOTINFO_CEILING 0x04000000u
struct handoff_summary { uint64_t usable_bytes; uint32_t mmap_entries; };
/* Never follows pointers from tags. Caller supplies accessible bytes. */
const char *handoff_validate(const void *bytes, size_t available,
                            uint64_t kernel_start, uint64_t kernel_end,
                            struct handoff_summary *out);
#endif

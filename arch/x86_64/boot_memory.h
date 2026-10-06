#ifndef GTOS_X64_BOOT_MEMORY_H
#define GTOS_X64_BOOT_MEMORY_H
#include "handoff.h"

#define BOOT_MEMORY_PAGE_SIZE 4096u
#define BOOT_MEMORY_FLOOR 0x00100000u
#define BOOT_MEMORY_CEILING BOOTINFO_CEILING
#define BOOT_MEMORY_MAX_FRAMES 2048u
#define BOOT_MEMORY_MIN_FRAMES 512u
#define BOOT_MEMORY_MAX_RETAINED 32u

struct boot_memory_range { uint64_t start, end; };
struct boot_memory_request {
    uint64_t kernel_start, kernel_end;
    uint64_t original_info_start, original_info_size;
    const struct boot_memory_range *retained;
    size_t retained_count;
    /* Zero selects the default. Tests can explicitly request a smaller pool. */
    uint32_t min_frames, max_frames;
};
struct boot_memory_selection {
    uint64_t frames[BOOT_MEMORY_MAX_FRAMES];
    uint64_t firmware_usable_bytes;
    uint32_t eligible_count, managed_count, mmap_entries;
};
/* BSP-only: the complete, resident private copy and request must remain immutable
 * throughout this call. Never follows tag pointers. All outputs are unchanged on
 * failure. Success selects ascending real frames and clears unused frame slots.
 * Retained ranges are trusted additional exclusions, never a caller free list.
 * The pool must invoke this parser itself, then retain its selection read-only. */
const char *boot_memory_select(const void *private_copy, size_t available,
                              const struct boot_memory_request *request,
                              struct boot_memory_selection *out);
#endif

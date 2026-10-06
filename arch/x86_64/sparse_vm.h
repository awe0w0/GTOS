#ifndef GTOS_X64_SPARSE_VM_H
#define GTOS_X64_SPARSE_VM_H
#include "frame_pool.h"
#define VM_PAGE UINT64_C(4096)
#define VM_ARENA_START UINT64_C(0xffff900000000000)
#define VM_ARENA_END UINT64_C(0xffff980000000000)
#define VM_MAX_REGIONS 64u
#define VM_MAX_COMMIT_PAGES 256u
#define VM_MAX_STAGED 262u
struct vm_handle { uint64_t space_id, generation; uint32_t slot; };
struct vm_region { struct vm_handle handle; uint64_t base, length; };
enum vm_perm { VM_NONE, VM_READ, VM_READ_WRITE };
enum vm_error {
    VM_OK, VM_ARGUMENT, VM_RANGE, VM_ALIGNMENT, VM_CONFLICT, VM_FOREIGN,
    VM_STALE, VM_NO_FRAMES, VM_LIMIT, VM_UNSUPPORTED, VM_STATE, VM_CORRUPT,
    VM_UNBACKED, VM_INJECTED, VM_ID_EXHAUSTED
};
enum vm_placement { VM_ANYWHERE, VM_EXACT };
struct vm_slot { uint64_t base, length, generation; uint32_t live; };
/* Indexed by managed physical frame, never by reserved virtual page. kind is
 * FRAME_DATA/PT/PD/PDPT or zero. Tables use va>>{21,30,39} as prefix. */
struct vm_backing {
    struct frame_id id;
    volatile uint64_t *alias;
    uint64_t va, generation;
    uint32_t slot, kind, permission, retiring;
};
struct vm_stage { struct vm_backing record; uint32_t index; };
struct vm_space {
    struct frame_pool *pool;
    volatile uint64_t *root;
    uint64_t space_id, flush_epoch, operation_visits;
    uint32_t ready, busy, staged_count, peak_staged;
    struct vm_slot regions[VM_MAX_REGIONS];
    struct vm_backing backing[BOOT_MEMORY_MAX_FRAMES];
    struct vm_stage staging[VM_MAX_STAGED];
};
struct vm_page_state { uint32_t backed; enum vm_perm permission; struct frame_id frame; };
struct vm_counts {
    uint64_t reserved_bytes, resident_bytes, resident_none_bytes, accessible_bytes;
    uint64_t metadata_bytes, flush_epoch, operation_visits;
    uint32_t reservations, data, pt, pd, pdpt, free_frames, managed, peak_staged;
};
/* Trusted BSP-only service, one active space for a given pool/root. A distinct
 * space receives a never-reused lifetime identity. No user/executable mappings.
 * Discard/reset/trim/split/punch are NOT implemented by this slice. */
enum vm_error vm_init(struct vm_space *, struct frame_pool *, volatile uint64_t *root);
enum vm_error vm_reserve(struct vm_space *, uint64_t bytes, uint64_t alignment,
    enum vm_placement, uint64_t exact, struct vm_region *);
enum vm_error vm_commit(struct vm_space *, struct vm_handle, uint64_t offset,
    uint64_t bytes, enum vm_perm);
enum vm_error vm_protect(struct vm_space *, struct vm_handle, uint64_t offset,
    uint64_t bytes, enum vm_perm);
enum vm_error vm_decommit(struct vm_space *, struct vm_handle, uint64_t offset, uint64_t bytes);
enum vm_error vm_release(struct vm_space *, struct vm_handle);
enum vm_error vm_query(struct vm_space *, struct vm_handle, uint64_t offset, struct vm_page_state *);
enum vm_error vm_stats(struct vm_space *, struct vm_counts *);
enum vm_error vm_audit(struct vm_space *);
/* Platform context callback uses this non-reentrant-free, read-only check.
 * It does not call any frame_pool API or inspect unowned physical memory. */
int vm_owned_hierarchy_valid(const struct vm_space *);
#endif

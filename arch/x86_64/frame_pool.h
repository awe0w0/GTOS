#ifndef GTOS_X64_FRAME_POOL_H
#define GTOS_X64_FRAME_POOL_H
#include "boot_memory.h"

#define FRAME_POOL_NX (UINT64_C(1) << 63)
/* IDs are physical-frame identities, not arbitrary dereferenceable pointers. */
struct frame_id { uint64_t physical, generation; };
enum frame_role {
    FRAME_FREE, FRAME_STAGED_DATA, FRAME_STAGED_TABLE, FRAME_DATA,
    FRAME_PT, FRAME_PD, FRAME_PDPT, FRAME_RETIRING, FRAME_ROLE_COUNT
};
enum frame_error {
    FRAME_OK, FRAME_BAD_ARGUMENT, FRAME_BAD_STATE, FRAME_BAD_CONTEXT,
    FRAME_BOOT_MEMORY, FRAME_ALIAS_CONFLICT, FRAME_CORRUPT,
    FRAME_EXHAUSTED, FRAME_INJECTED, FRAME_FOREIGN, FRAME_STALE,
    FRAME_WRONG_OWNER, FRAME_WRONG_ROLE, FRAME_ID_EXHAUSTED
};
/* Trusted platform backend. The production instance uses the real low PTs and
 * current BSP registers. Test backends never constitute guest RAM evidence. */
struct frame_platform {
    int (*context_ok)(void *);
    uint64_t (*read_leaf)(void *, uint64_t physical);
    void (*write_leaf)(void *, uint64_t physical, uint64_t entry);
    void (*flush)(void *);
    volatile unsigned char *(*alias)(void *, uint64_t physical);
    void *opaque;
};
struct frame_record { uint64_t owner, generation; enum frame_role role; };
struct frame_stats {
    uint64_t firmware_usable_bytes;
    uint32_t eligible, managed, roles[FRAME_ROLE_COUNT];
    uint32_t permanent_aliases, borrowed_table_frames;
};
struct frame_pool {
    struct boot_memory_selection selection;
    struct frame_record records[BOOT_MEMORY_MAX_FRAMES];
    struct frame_platform platform;
    uint64_t generation;
    uint32_t roles[FRAME_ROLE_COUNT];
    uint32_t ready, busy, fail_nth, allocation_attempt;
};
/* pool must initially be zeroed and privately retained in kernel memory.
 * Initialization is all-or-nothing, including alias rollback on audit failure.
 * bytes is the already resident private copy, never the revoked loader buffer. */
enum frame_error frame_pool_init(struct frame_pool *, const void *bytes, size_t available,
    const struct boot_memory_request *, const struct frame_platform *, const char **boot_error);
enum frame_error frame_pool_audit(struct frame_pool *);
enum frame_error frame_pool_stats(struct frame_pool *, struct frame_stats *);
enum frame_error frame_pool_allocate(struct frame_pool *, enum frame_role staged_role,
    uint64_t owner, struct frame_id *);
enum frame_error frame_pool_promote(struct frame_pool *, struct frame_id, uint64_t owner,
    enum frame_role final_role);
/* Cancel staging directly. Live DATA/PT/PD/PDPT must first be unlinked by their
 * owner, then marked RETIRING. reclaim reloads CR3 before ANY retired frame is
 * freed; borrowed bootstrap tables are absent from this pool entirely. */
enum frame_error frame_pool_cancel(struct frame_pool *, struct frame_id, uint64_t owner);
enum frame_error frame_pool_retire(struct frame_pool *, struct frame_id, uint64_t owner,
    enum frame_role expected_role);
enum frame_error frame_pool_reclaim(struct frame_pool *, uint32_t *released);
/* A checked privileged alias for a currently owned ID, never for FREE frames.
 * R/NONE at future service VAs will not restrict this trusted supervisor alias. */
enum frame_error frame_pool_alias(struct frame_pool *, struct frame_id, uint64_t owner,
    volatile unsigned char **);
/* Deterministic allocation-failure test hook; zero disables. No physical OOM claim. */
enum frame_error frame_pool_fail_after(struct frame_pool *, uint32_t nth);
#endif

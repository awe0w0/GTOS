/* Host evidence for the SAME boot selector and ownership core linked in the
 * guest. Byte-backed RAM and bounded PTs are mocks, not guest-RAM/TLB proof. */
#include "frame_pool.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PAGE BOOT_MEMORY_PAGE_SIZE
#define LEAVES (BOOTINFO_CEILING / PAGE)
#define SMALL_POOL 8u
#define OWNER UINT64_C(0x5a170001)
#define OTHER_OWNER UINT64_C(0x5a170002)
#define CANARY UINT64_C(0xbaadf00dfeed1234)
#define OK(call) assert((call) == FRAME_OK)
#define ERROR(call, want) assert((call) == (want))

static struct frame_pool pool, snapshot;
static unsigned char handoff[512];
static size_t handoff_size;
static struct boot_memory_request request;
static struct boot_memory_range retained;
static const uint64_t chosen[SMALL_POOL] = {
    0x161000, 0x163000, 0x167000, 0x16b000,
    0x16c000, 0x16d000, 0x16e000, 0x16f000
};
static struct {
    uint64_t leaves[LEAVES], baseline[LEAVES];
    unsigned char ram[BOOT_MEMORY_MAX_FRAMES][PAGE];
    unsigned context_calls, reads, writes, flushes, aliases;
    unsigned writes_since_flush, recursive_mask;
    int context, recurse, sabotage, null_alias;
    uint32_t expect_retired, expect_free;
} backend;
static struct frame_platform platform;

enum callback_kind { IN_CONTEXT, IN_READ, IN_WRITE, IN_FLUSH, IN_ALIAS };
static void callback(enum callback_kind kind) {
    assert(pool.busy == 1);
    if (kind != IN_CONTEXT) assert(backend.context);
    if (backend.recurse) {
        /* No platform callback may reopen the allocator, including its context
         * check. Each attempted recursion must stop before touching the backend. */
        ERROR(frame_pool_audit(&pool), FRAME_BAD_STATE);
        ERROR(frame_pool_fail_after(&pool, 1), FRAME_BAD_STATE);
        backend.recursive_mask |= 1u << kind;
    }
}
static size_t leaf_index(uint64_t physical) {
    assert(!(physical & (PAGE-1)));
    assert(physical >= BOOT_MEMORY_FLOOR && physical < BOOTINFO_CEILING);
    return (size_t)(physical / PAGE);
}
static int mock_context(void *opaque) {
    assert(opaque == &backend);
    callback(IN_CONTEXT);
    ++backend.context_calls;
    return backend.context;
}
static uint64_t mock_read(void *opaque, uint64_t physical) {
    assert(opaque == &backend);
    callback(IN_READ);
    ++backend.reads;
    return backend.leaves[leaf_index(physical)];
}
static void mock_write(void *opaque, uint64_t physical, uint64_t entry) {
    assert(opaque == &backend);
    callback(IN_WRITE);
    /* Every published nonzero alias is privileged, writable and NX. Neither
     * software nonpresent payloads nor pre-existing mappings may be replaced. */
    size_t index = leaf_index(physical);
    assert(!entry || entry == (physical | FRAME_POOL_NX | 3));
    assert(!entry || !backend.leaves[index]);
    backend.leaves[index] = entry;
    ++backend.writes;
    ++backend.writes_since_flush;
}
static void mock_flush(void *opaque) {
    assert(opaque == &backend);
    callback(IN_FLUSH);
    if (backend.expect_retired) {
        /* This is a callback-time test: examining counts only AFTER reclaim
         * would not detect an implementation that freed before invalidating. */
        assert(pool.roles[FRAME_RETIRING] == backend.expect_retired);
        assert(pool.roles[FRAME_FREE] == backend.expect_free);
        unsigned found = 0;
        for (uint32_t i = 0; i < pool.selection.managed_count; ++i)
            if (pool.records[i].role == FRAME_RETIRING) {
                assert(pool.records[i].owner && pool.records[i].generation);
                ++found;
            }
        assert(found == backend.expect_retired);
    }
    ++backend.flushes;
    backend.writes_since_flush = 0;
    /* Corrupt after installation but before its audit. Initialization must
     * roll back ALL new leaves and invalidate a second time. */
    if (backend.sabotage && backend.flushes == 1) {
        uint64_t last = pool.selection.frames[pool.selection.managed_count-1];
        backend.leaves[leaf_index(last)] ^= 4;
    }
}
static volatile unsigned char *mock_alias(void *opaque, uint64_t physical) {
    assert(opaque == &backend);
    callback(IN_ALIAS);
    assert(!backend.writes_since_flush && backend.flushes);
    assert((backend.leaves[leaf_index(physical)] & ~UINT64_C(0x60)) ==
           (physical | FRAME_POOL_NX | 3));
    ++backend.aliases;
    if (backend.null_alias) return NULL;
    for (uint32_t i = 0; i < pool.selection.managed_count; ++i)
        if (pool.selection.frames[i] == physical) {
            assert(pool.records[i].role != FRAME_RETIRING);
            /* Allocation obtains its alias while still FREE, before an owner
             * or generation has been published. */
            if (pool.records[i].role == FRAME_FREE) assert(!pool.records[i].owner);
            return backend.ram[i];
        }
    assert(!"alias outside the selected physical pool");
    return NULL;
}
static void w32(unsigned off, uint32_t value) {
    assert(off + 4 <= sizeof(handoff));
    for (unsigned i=0; i<4; ++i) handoff[off+i] = (unsigned char)(value >> (i*8));
}
static void w64(unsigned off, uint64_t value) {
    w32(off, (uint32_t)value); w32(off+4, (uint32_t)(value >> 32));
}
static void mmap_entry(unsigned off, uint64_t start, uint64_t length, uint32_t type) {
    w64(off, start); w64(off+8, length); w32(off+16, type);
}
static void fixture(void) {
    memset(&pool, 0, sizeof(pool));
    memset(&backend, 0, sizeof(backend));
    memset(handoff, 0, sizeof(handoff));
    /* Disjoint real map, deliberately unsorted; includes reserved/ACPI/NVS/
     * unknown pages and usable memory beyond the supported low pool window. */
    w32(8, 6); w32(12, 16+7*24); w32(16, 24);
    mmap_entry(24,     0x169000, 0x17000, 1);
    mmap_entry(24+24,  0x168000, 0x1000, 2);
    mmap_entry(24+48,  0x100000, 0x68000, 1);
    mmap_entry(24+72,  0x180000, 0x1000, 3);
    mmap_entry(24+96,  0x181000, 0x1000, 4);
    mmap_entry(24+120, 0x182000, 0x1000, 99);
    mmap_entry(24+144, BOOTINFO_CEILING, 0x6000, 1);
    unsigned off = 8+16+7*24;
    w32(off, 3); w32(off+4, 26);
    w32(off+8, 0x165ff8); w32(off+12, 0x166005);
    memcpy(handoff+off+16, "pool test", 10); off += 32;
    w32(off, 8); w32(off+4, 32); w64(off+8, 0x169f00);
    w32(off+16, 256); w32(off+20, 128); w32(off+24, 2);
    handoff[off+28] = 16; handoff[off+29] = 2; off += 32;
    w32(off, 0); w32(off+4, 8); handoff_size = off+8; w32(0, (uint32_t)handoff_size);
    retained = (struct boot_memory_range){0x162080, 0x162081};
    request = (struct boot_memory_request){
        .kernel_start = 0x100000, .kernel_end = 0x160123,
        .original_info_start = 0x164020, .original_info_size = handoff_size,
        .retained = &retained, .retained_count = 1,
        .min_frames = 1, .max_frames = SMALL_POOL
    };
    platform = (struct frame_platform){mock_context, mock_read, mock_write,
                                      mock_flush, mock_alias, &backend};
    backend.context = 1;
    /* These are outside selection. Neither init, rollback nor reclamation may
     * touch any existing kernel/bootstrap leaf or software nonpresent value. */
    backend.leaves[0x100000/PAGE] = 0x100001;
    backend.leaves[0x168000/PAGE] = UINT64_C(0xdead000);
    backend.leaves[LEAVES-1] = (BOOTINFO_CEILING-PAGE) | FRAME_POOL_NX | 3;
    memcpy(backend.baseline, backend.leaves, sizeof(backend.leaves));
    memset(backend.ram, 0xa5, sizeof(backend.ram));
}
static void initialize(void) {
    const char *boot_error = NULL;
    OK(frame_pool_init(&pool, handoff, handoff_size, &request, &platform, &boot_error));
    assert(!boot_error && pool.ready == 1 && !pool.busy);
}
static void start(void) { fixture(); initialize(); }
static void assert_bytes(const unsigned char *bytes, size_t length, unsigned char value) {
    for (size_t i=0; i<length; ++i) assert(bytes[i] == value);
}
static void assert_empty(void) {
    static const struct frame_pool empty;
    assert(!memcmp(&pool, &empty, sizeof(pool)));
}
static void assert_baseline(void) {
    assert(!memcmp(backend.leaves, backend.baseline, sizeof(backend.leaves)));
}
static void assert_aliases(void) {
    unsigned found = 0;
    for (size_t index=0; index<LEAVES; ++index) {
        uint64_t expected = backend.baseline[index];
        for (uint32_t slot=0; slot<pool.selection.managed_count; ++slot)
            if (pool.selection.frames[slot]/PAGE == index) {
                expected = pool.selection.frames[slot] | FRAME_POOL_NX | 3;
                ++found;
            }
        assert((backend.leaves[index] & ~UINT64_C(0x60)) == (expected & ~UINT64_C(0x60)));
    }
    assert(found == pool.selection.managed_count);
}
static void assert_roles(const uint32_t *roles) {
    struct frame_stats stats;
    memset(&stats, 0, sizeof(stats));
    OK(frame_pool_stats(&pool, &stats));
    uint32_t total = 0;
    for (unsigned i=0; i<FRAME_ROLE_COUNT; ++i) {
        assert(stats.roles[i] == roles[i]);
        total += roles[i];
    }
    assert(total == stats.managed);
    assert(stats.managed == SMALL_POOL && stats.eligible == 24);
    assert(stats.firmware_usable_bytes == 0x85000);
    assert(stats.permanent_aliases == SMALL_POOL && stats.borrowed_table_frames == 35);
    OK(frame_pool_audit(&pool));
}
static void assert_all_free(void) {
    uint32_t roles[FRAME_ROLE_COUNT] = {0}; roles[FRAME_FREE] = SMALL_POOL;
    assert_roles(roles);
}
static void assert_unchanged(void) {
    assert(!memcmp(&pool, &snapshot, sizeof(pool)));
    assert(!pool.busy);
}
static void test_boot_selection_and_aliases(void) {
    start();
    assert(pool.selection.mmap_entries == 7);
    assert(!memcmp(pool.selection.frames, chosen, sizeof(chosen)));
    for (unsigned i=SMALL_POOL; i<BOOT_MEMORY_MAX_FRAMES; ++i) assert(!pool.selection.frames[i]);
    assert(backend.writes == SMALL_POOL && backend.flushes == 1 && !backend.aliases);
    assert_bytes(backend.ram[0], sizeof(backend.ram), 0xa5);
    assert_aliases(); assert_all_free();
    snapshot = pool;
    ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, &platform, NULL), FRAME_BAD_STATE);
    assert_unchanged();
    /* Independent full-window checks include null, guards, original information,
     * retained partial pages, module payload, framebuffer and non-type-1 holes. */
    const uint64_t excluded[] = {0, 0x160000, 0x162000, 0x164000, 0x165000,
                                0x166000, 0x169000, 0x16a000, 0x180000, 0x181000, 0x182000};
    for (unsigned i=0; i<sizeof(excluded)/sizeof(excluded[0]); ++i)
        assert(backend.leaves[excluded[i]/PAGE] == backend.baseline[excluded[i]/PAGE]);
}
static void test_conflict_preflight_and_rollback(void) {
    /* Test every candidate, including the last, with a present and a nonpresent
     * software-owned leaf. No earlier candidate may be partially overwritten. */
    for (unsigned slot=0; slot<SMALL_POOL; ++slot)
        for (unsigned present=0; present<2; ++present) {
            fixture();
            backend.leaves[chosen[slot]/PAGE] = present ? (chosen[slot] | 1) : 0x200;
            memcpy(backend.baseline, backend.leaves, sizeof(backend.leaves));
            ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, &platform, NULL), FRAME_ALIAS_CONFLICT);
            assert(!backend.writes && !backend.flushes && !backend.aliases);
            assert_empty(); assert_baseline();
            assert_bytes(backend.ram[0], sizeof(backend.ram), 0xa5);
        }
    fixture(); backend.sabotage = 1;
    ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, &platform, NULL), FRAME_CORRUPT);
    assert(backend.writes == 2*SMALL_POOL && backend.flushes == 2 && !backend.aliases);
    assert(!backend.writes_since_flush); assert_empty(); assert_baseline();
    /* A rolled-back initialization can be retried, with no leaked aliases. */
    backend.sabotage = 0; initialize(); assert_all_free(); assert_aliases();
}
static void test_allocate_exhaust_cancel_reuse(void) {
    start();
    struct frame_id ids[SMALL_POOL];
    uint32_t roles[FRAME_ROLE_COUNT] = {0}; roles[FRAME_FREE] = SMALL_POOL;
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        enum frame_role staged = i&1 ? FRAME_STAGED_TABLE : FRAME_STAGED_DATA;
        OK(frame_pool_allocate(&pool, staged, OWNER+i, &ids[i]));
        assert(ids[i].physical == chosen[i] && ids[i].generation == i+1);
        assert_bytes(backend.ram[i], PAGE, 0);
        for (unsigned j=i+1; j<SMALL_POOL; ++j) assert_bytes(backend.ram[j], PAGE, 0xa5);
        --roles[FRAME_FREE]; ++roles[staged]; assert_roles(roles);
        volatile unsigned char *address = NULL;
        OK(frame_pool_alias(&pool, ids[i], OWNER+i, &address));
        assert(address == backend.ram[i]);
        for (unsigned j=0; j<PAGE; ++j) address[j] = (unsigned char)(i+1);
    }
    assert(backend.writes == SMALL_POOL && backend.flushes == 1);
    snapshot = pool;
    struct frame_id out = {CANARY, CANARY}, before = out;
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &out), FRAME_EXHAUSTED);
    assert(!memcmp(&out, &before, sizeof(out))); assert_unchanged();
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        OK(frame_pool_cancel(&pool, ids[i], OWNER+i));
        --roles[i&1 ? FRAME_STAGED_TABLE : FRAME_STAGED_DATA]; ++roles[FRAME_FREE];
        assert_roles(roles); assert_bytes(backend.ram[i], PAGE, (unsigned char)(i+1));
        snapshot = pool;
        ERROR(frame_pool_cancel(&pool, ids[i], OWNER+i), FRAME_STALE); assert_unchanged();
    }
    assert(backend.flushes == 1); /* unpublished staging needs no TLB reclaim */
    OK(frame_pool_allocate(&pool, FRAME_STAGED_TABLE, OTHER_OWNER, &out));
    assert(out.physical == ids[0].physical && out.generation > ids[SMALL_POOL-1].generation);
    assert_bytes(backend.ram[0], PAGE, 0);
    snapshot = pool;
    ERROR(frame_pool_cancel(&pool, ids[0], OWNER), FRAME_STALE); assert_unchanged();
    OK(frame_pool_cancel(&pool, out, OTHER_OWNER)); assert_all_free(); assert_aliases();
}
static void test_live_roles_retirement_and_reclaim(void) {
    start();
    const enum frame_role finals[SMALL_POOL] = {
        FRAME_DATA, FRAME_PT, FRAME_PD, FRAME_PDPT,
        FRAME_DATA, FRAME_PT, FRAME_PD, FRAME_PDPT
    };
    struct frame_id ids[SMALL_POOL];
    uint32_t roles[FRAME_ROLE_COUNT] = {0}; roles[FRAME_FREE] = SMALL_POOL;
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        OK(frame_pool_allocate(&pool, finals[i] == FRAME_DATA ? FRAME_STAGED_DATA : FRAME_STAGED_TABLE,
                               OWNER+i, &ids[i]));
        OK(frame_pool_promote(&pool, ids[i], OWNER+i, finals[i]));
        --roles[FRAME_FREE]; ++roles[finals[i]]; assert_roles(roles);
        memset(backend.ram[i], (int)i+11, PAGE);
        snapshot = pool;
        ERROR(frame_pool_cancel(&pool, ids[i], OWNER+i), FRAME_WRONG_ROLE); assert_unchanged();
        ERROR(frame_pool_promote(&pool, ids[i], OWNER+i, finals[i]), FRAME_WRONG_ROLE); assert_unchanged();
    }
    assert(!roles[FRAME_FREE]);
    uint32_t released = 99; OK(frame_pool_reclaim(&pool, &released));
    assert(!released && backend.flushes == 1);
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        snapshot = pool;
        ERROR(frame_pool_retire(&pool, ids[i], OWNER+i,
                               finals[i] == FRAME_PT ? FRAME_PD : FRAME_PT), FRAME_WRONG_ROLE);
        assert_unchanged();
        OK(frame_pool_retire(&pool, ids[i], OWNER+i, finals[i]));
        --roles[finals[i]]; ++roles[FRAME_RETIRING]; assert_roles(roles);
        snapshot = pool;
        volatile unsigned char *alias = (volatile unsigned char *)(uintptr_t)CANARY;
        ERROR(frame_pool_alias(&pool, ids[i], OWNER+i, &alias), FRAME_WRONG_ROLE);
        assert(alias == (volatile unsigned char *)(uintptr_t)CANARY); assert_unchanged();
        ERROR(frame_pool_cancel(&pool, ids[i], OWNER+i), FRAME_WRONG_ROLE); assert_unchanged();
        ERROR(frame_pool_retire(&pool, ids[i], OWNER+i, finals[i]), FRAME_WRONG_ROLE); assert_unchanged();
    }
    snapshot = pool;
    struct frame_id out = {CANARY, CANARY};
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OTHER_OWNER, &out), FRAME_EXHAUSTED);
    assert(out.physical == CANARY && out.generation == CANARY); assert_unchanged();
    assert(backend.flushes == 1); /* retire alone never frees or reuses a frame */
    backend.expect_retired = SMALL_POOL; backend.expect_free = 0;
    OK(frame_pool_reclaim(&pool, &released));
    backend.expect_retired = 0;
    assert(released == SMALL_POOL && backend.flushes == 2);
    assert_all_free(); assert_aliases();
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        assert_bytes(backend.ram[i], PAGE, (unsigned char)(i+11));
        snapshot = pool;
        ERROR(frame_pool_retire(&pool, ids[i], OWNER+i, finals[i]), FRAME_STALE); assert_unchanged();
    }
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        OK(frame_pool_allocate(&pool, FRAME_STAGED_TABLE, OTHER_OWNER, &out));
        assert(out.physical == ids[i].physical && out.generation > ids[i].generation);
        assert_bytes(backend.ram[i], PAGE, 0);
        snapshot = pool;
        ERROR(frame_pool_promote(&pool, ids[i], OWNER+i, finals[i]), FRAME_STALE); assert_unchanged();
    }
}
static void test_identity_owner_and_arguments(void) {
    start();
    struct frame_id data, table;
    OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &data));
    OK(frame_pool_allocate(&pool, FRAME_STAGED_TABLE, OWNER, &table));
    snapshot = pool;
    volatile unsigned char *alias = (volatile unsigned char *)(uintptr_t)CANARY;
    ERROR(frame_pool_alias(&pool, data, OTHER_OWNER, &alias), FRAME_WRONG_OWNER);
    assert(alias == (volatile unsigned char *)(uintptr_t)CANARY); assert_unchanged();
    ERROR(frame_pool_cancel(&pool, data, OTHER_OWNER), FRAME_WRONG_OWNER); assert_unchanged();
    ERROR(frame_pool_promote(&pool, table, OTHER_OWNER, FRAME_PT), FRAME_WRONG_OWNER); assert_unchanged();
    ERROR(frame_pool_retire(&pool, data, OTHER_OWNER, FRAME_DATA), FRAME_WRONG_OWNER); assert_unchanged();
    ERROR(frame_pool_promote(&pool, data, OWNER, FRAME_PT), FRAME_WRONG_ROLE); assert_unchanged();
    ERROR(frame_pool_promote(&pool, table, OWNER, FRAME_DATA), FRAME_WRONG_ROLE); assert_unchanged();
    ERROR(frame_pool_retire(&pool, data, OWNER, FRAME_DATA), FRAME_WRONG_ROLE); assert_unchanged();
    const uint64_t foreign[] = {0, 0x100000, 0x160000, 0x162000, 0x164000,
        0x165000, 0x166000, 0x168000, 0x169000, 0x16a000, 0x180000,
        0x181000, 0x182000, BOOTINFO_CEILING, UINT64_MAX, 0x161001};
    for (unsigned i=0; i<sizeof(foreign)/sizeof(foreign[0]); ++i) {
        struct frame_id id = {foreign[i], data.generation};
        ERROR(frame_pool_cancel(&pool, id, OWNER), FRAME_FOREIGN); assert_unchanged();
        ERROR(frame_pool_retire(&pool, id, OWNER, FRAME_DATA), FRAME_FOREIGN); assert_unchanged();
        ERROR(frame_pool_promote(&pool, id, OWNER, FRAME_DATA), FRAME_FOREIGN); assert_unchanged();
        ERROR(frame_pool_alias(&pool, id, OWNER, &alias), FRAME_FOREIGN); assert_unchanged();
        assert(alias == (volatile unsigned char *)(uintptr_t)CANARY);
    }
    struct frame_id stale = data; ++stale.generation;
    ERROR(frame_pool_cancel(&pool, stale, OWNER), FRAME_STALE); assert_unchanged();
    stale.generation = 0;
    ERROR(frame_pool_promote(&pool, stale, OWNER, FRAME_DATA), FRAME_STALE); assert_unchanged();
    struct frame_id out = {CANARY, CANARY};
    for (int role=-1; role<FRAME_ROLE_COUNT+1; ++role)
        if (role != FRAME_STAGED_DATA && role != FRAME_STAGED_TABLE) {
            ERROR(frame_pool_allocate(&pool, (enum frame_role)role, OWNER, &out), FRAME_BAD_ARGUMENT);
            assert(out.physical == CANARY && out.generation == CANARY); assert_unchanged();
        }
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_DATA, 0, &out), FRAME_BAD_ARGUMENT); assert_unchanged();
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, NULL), FRAME_BAD_ARGUMENT); assert_unchanged();
    ERROR(frame_pool_alias(&pool, data, 0, &alias), FRAME_BAD_ARGUMENT); assert_unchanged();
    ERROR(frame_pool_cancel(&pool, data, 0), FRAME_BAD_ARGUMENT); assert_unchanged();
    ERROR(frame_pool_alias(&pool, data, OWNER, NULL), FRAME_BAD_ARGUMENT); assert_unchanged();
    ERROR(frame_pool_stats(&pool, NULL), FRAME_BAD_ARGUMENT); assert_unchanged();
    ERROR(frame_pool_reclaim(&pool, NULL), FRAME_BAD_ARGUMENT); assert_unchanged();
    for (int role=-1; role<FRAME_ROLE_COUNT+1; ++role)
        if (role != FRAME_DATA && role != FRAME_PT && role != FRAME_PD && role != FRAME_PDPT) {
            ERROR(frame_pool_promote(&pool, data, OWNER, (enum frame_role)role), FRAME_BAD_ARGUMENT); assert_unchanged();
            ERROR(frame_pool_retire(&pool, data, OWNER, (enum frame_role)role), FRAME_BAD_ARGUMENT); assert_unchanged();
        }
    OK(frame_pool_cancel(&pool, data, OWNER)); OK(frame_pool_cancel(&pool, table, OWNER));
    assert_all_free();
}
static void test_failure_injection_and_rollback(void) {
    /* This is a staged allocation sequence, not an implemented VM transaction.
     * At every fallible index the caller cancels staging, preserving existing
     * live ownership, byte contents, aliases and all accounting categories. */
    const enum frame_role sequence[] = {FRAME_STAGED_TABLE, FRAME_STAGED_TABLE,
        FRAME_STAGED_TABLE, FRAME_STAGED_DATA, FRAME_STAGED_DATA};
    for (unsigned fail=1; fail<=sizeof(sequence)/sizeof(sequence[0]); ++fail) {
        start(); struct frame_id live;
        OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &live));
        OK(frame_pool_promote(&pool, live, OWNER, FRAME_DATA));
        memset(backend.ram[0], 0x73, PAGE);
        struct frame_stats before, after;
        OK(frame_pool_stats(&pool, &before));
        struct frame_record live_before = pool.records[0];
        OK(frame_pool_fail_after(&pool, fail));
        struct frame_id staged[5]; unsigned allocated = 0;
        for (unsigned step=0; step<sizeof(sequence)/sizeof(sequence[0]); ++step) {
            staged[step] = (struct frame_id){CANARY, CANARY};
            enum frame_error result = frame_pool_allocate(&pool, sequence[step], OTHER_OWNER, &staged[step]);
            if (step+1 == fail) {
                assert(result == FRAME_INJECTED);
                assert(staged[step].physical == CANARY && staged[step].generation == CANARY);
                break;
            }
            assert(result == FRAME_OK); ++allocated;
        }
        assert(allocated == fail-1 && pool.allocation_attempt == fail);
        while (allocated) { --allocated; OK(frame_pool_cancel(&pool, staged[allocated], OTHER_OWNER)); }
        OK(frame_pool_stats(&pool, &after));
        assert(!memcmp(&before, &after, sizeof(before)));
        assert(!memcmp(&live_before, &pool.records[0], sizeof(live_before)));
        assert_bytes(backend.ram[0], PAGE, 0x73); assert_aliases();
        assert(backend.flushes == 1);
        OK(frame_pool_fail_after(&pool, 0));
        for (unsigned step=0; step<sizeof(sequence)/sizeof(sequence[0]); ++step) {
            OK(frame_pool_allocate(&pool, sequence[step], OTHER_OWNER, &staged[step]));
            assert_bytes(backend.ram[step+1], PAGE, 0);
        }
        for (unsigned step=0; step<sizeof(sequence)/sizeof(sequence[0]); ++step)
            OK(frame_pool_cancel(&pool, staged[step], OTHER_OWNER));
        OK(frame_pool_stats(&pool, &after)); assert(!memcmp(&before, &after, sizeof(before)));
        assert_bytes(backend.ram[0], PAGE, 0x73);
        /* Reset the deterministic hook; the first attempt fails again. */
        OK(frame_pool_fail_after(&pool, 1));
        struct frame_id out = {CANARY, CANARY};
        ERROR(frame_pool_allocate(&pool, FRAME_STAGED_TABLE, OTHER_OWNER, &out), FRAME_INJECTED);
        assert(out.physical == CANARY && out.generation == CANARY);
    }
}
static void test_context_and_recursion(void) {
    fixture(); backend.context = 0;
    ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, &platform, NULL), FRAME_BAD_CONTEXT);
    assert_empty(); assert_baseline();
    assert(backend.context_calls == 1 && !backend.reads && !backend.writes && !backend.flushes && !backend.aliases);
    backend.context = 1; initialize();
    struct frame_id id; OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &id));
    snapshot = pool;
    backend.context = 0;
    unsigned reads = backend.reads, writes = backend.writes, flushes = backend.flushes, aliases = backend.aliases;
    struct frame_stats stats, stats_before; memset(&stats, 0x5c, sizeof(stats)); stats_before = stats;
    volatile unsigned char *alias = (volatile unsigned char *)(uintptr_t)CANARY;
    struct frame_id out = {CANARY, CANARY}; uint32_t released = 0xfeed;
    ERROR(frame_pool_audit(&pool), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_stats(&pool, &stats), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &out), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_promote(&pool, id, OWNER, FRAME_DATA), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_cancel(&pool, id, OWNER), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_retire(&pool, id, OWNER, FRAME_DATA), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_reclaim(&pool, &released), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_alias(&pool, id, OWNER, &alias), FRAME_BAD_CONTEXT); assert_unchanged();
    ERROR(frame_pool_fail_after(&pool, 1), FRAME_BAD_CONTEXT); assert_unchanged();
    assert(!memcmp(&stats, &stats_before, sizeof(stats)) && released == 0xfeed);
    assert(out.physical == CANARY && out.generation == CANARY);
    assert(alias == (volatile unsigned char *)(uintptr_t)CANARY);
    assert(backend.reads == reads && backend.writes == writes && backend.flushes == flushes && backend.aliases == aliases);
    fixture(); backend.recurse = 1; initialize();
    OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &id));
    OK(frame_pool_promote(&pool, id, OWNER, FRAME_DATA));
    OK(frame_pool_retire(&pool, id, OWNER, FRAME_DATA));
    backend.expect_retired = 1; backend.expect_free = SMALL_POOL-1;
    OK(frame_pool_reclaim(&pool, &released)); backend.expect_retired = 0;
    assert(released == 1 && backend.recursive_mask == (1u << (IN_ALIAS+1))-1);
    assert_all_free();
    pool.busy = 1; ERROR(frame_pool_audit(&pool), FRAME_BAD_STATE); assert(pool.busy == 1); pool.busy = 0;
    pool.ready = 2; ERROR(frame_pool_audit(&pool), FRAME_BAD_STATE); assert(!pool.busy); pool.ready = 1;
    ERROR(frame_pool_audit(NULL), FRAME_BAD_STATE);
    fixture(); ERROR(frame_pool_audit(&pool), FRAME_BAD_STATE);
}
static void assert_corrupt_fails_closed(void) {
    snapshot = pool;
    unsigned writes = backend.writes, flushes = backend.flushes, aliases = backend.aliases;
    struct frame_id out = {CANARY, CANARY};
    struct frame_stats stats, before; memset(&stats, 0x5c, sizeof(stats)); before = stats;
    volatile unsigned char *alias = (volatile unsigned char *)(uintptr_t)CANARY;
    uint32_t released = 0xfeed;
    struct frame_id id = {chosen[0], 1};
    ERROR(frame_pool_audit(&pool), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_stats(&pool, &stats), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &out), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_alias(&pool, id, OWNER, &alias), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_reclaim(&pool, &released), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_cancel(&pool, id, OWNER), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_retire(&pool, id, OWNER, FRAME_DATA), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_promote(&pool, id, OWNER, FRAME_DATA), FRAME_CORRUPT); assert_unchanged();
    ERROR(frame_pool_fail_after(&pool, 1), FRAME_CORRUPT); assert_unchanged();
    assert(out.physical == CANARY && out.generation == CANARY && released == 0xfeed);
    assert(!memcmp(&stats, &before, sizeof(stats)));
    assert(alias == (volatile unsigned char *)(uintptr_t)CANARY);
    assert(backend.writes == writes && backend.flushes == flushes && backend.aliases == aliases);
}
static void test_metadata_and_leaf_corruption(void) {
    start(); struct frame_id id; OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &id));
    struct frame_pool valid = pool;
    for (unsigned role=0; role<FRAME_ROLE_COUNT; ++role) {
        ++pool.roles[role]; assert_corrupt_fails_closed(); pool = valid;
    }
    pool.records[0].role = FRAME_ROLE_COUNT; assert_corrupt_fails_closed(); pool = valid;
    pool.records[0].role = (enum frame_role)-1; assert_corrupt_fails_closed(); pool = valid;
    pool.records[0].owner = 0; assert_corrupt_fails_closed(); pool = valid;
    pool.records[1].owner = OWNER; assert_corrupt_fails_closed(); pool = valid;
    pool.records[0].generation = 0; assert_corrupt_fails_closed(); pool = valid;
    pool.records[0].generation = pool.generation+1; assert_corrupt_fails_closed(); pool = valid;
    pool.records[1].generation = pool.generation+1; assert_corrupt_fails_closed(); pool = valid;
    pool.generation = 0; assert_corrupt_fails_closed(); pool = valid;
    pool.selection.managed_count = 0; assert_corrupt_fails_closed(); pool = valid;
    pool.selection.managed_count = BOOT_MEMORY_MAX_FRAMES+1; assert_corrupt_fails_closed(); pool = valid;
    pool.selection.eligible_count = SMALL_POOL-1; assert_corrupt_fails_closed(); pool = valid;
    const uint64_t invalid[] = {0, BOOT_MEMORY_FLOOR-1, BOOTINFO_CEILING, chosen[0]+1, UINT64_MAX};
    for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); ++i) {
        pool.selection.frames[0] = invalid[i]; assert_corrupt_fails_closed(); pool = valid;
    }
    pool.selection.frames[1] = pool.selection.frames[0]; assert_corrupt_fails_closed(); pool = valid;
    /* Exact leaf policy: ONLY accessed/dirty are mutable, all other changed
     * bits (including writable, NX, U/S, global, PAT and address) fail closed. */
    uint64_t original = backend.leaves[chosen[SMALL_POOL-1]/PAGE];
    for (unsigned bit=0; bit<64; ++bit) {
        backend.leaves[chosen[SMALL_POOL-1]/PAGE] = original ^ (UINT64_C(1) << bit);
        if (bit == 5 || bit == 6) OK(frame_pool_audit(&pool));
        else assert_corrupt_fails_closed();
    }
    backend.leaves[chosen[SMALL_POOL-1]/PAGE] = original | 0x60;
    OK(frame_pool_audit(&pool)); assert_aliases();
    backend.null_alias = 1; snapshot = pool;
    struct frame_id out = {CANARY, CANARY};
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_TABLE, OWNER, &out), FRAME_CORRUPT); assert_unchanged();
    assert(out.physical == CANARY && out.generation == CANARY);
    volatile unsigned char *alias = (volatile unsigned char *)(uintptr_t)CANARY;
    ERROR(frame_pool_alias(&pool, id, OWNER, &alias), FRAME_CORRUPT); assert_unchanged();
    assert(alias == (volatile unsigned char *)(uintptr_t)CANARY);
    assert_bytes(backend.ram[1], PAGE, 0xa5);
}
static void test_generation_exhaustion(void) {
    start(); pool.generation = UINT64_MAX-1;
    struct frame_id last; OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &last));
    assert(last.generation == UINT64_MAX);
    OK(frame_pool_cancel(&pool, last, OWNER)); snapshot = pool;
    struct frame_id out = {CANARY, CANARY};
    ERROR(frame_pool_allocate(&pool, FRAME_STAGED_TABLE, OTHER_OWNER, &out), FRAME_ID_EXHAUSTED);
    assert(out.physical == CANARY && out.generation == CANARY); assert_unchanged();
    ERROR(frame_pool_cancel(&pool, last, OWNER), FRAME_STALE); assert_unchanged();
    assert_all_free();
}
static void test_private_selection_lifetime(void) {
    start();
    /* Only copied physical identities survive initialization. Neither the
     * original loader tags nor request/exclusion pointers are retained. */
    memset(handoff, 0xff, sizeof(handoff));
    memset(&request, 0xff, sizeof(request));
    retained.start = retained.end = UINT64_MAX;
    struct frame_id id;
    OK(frame_pool_allocate(&pool, FRAME_STAGED_DATA, OWNER, &id));
    assert(id.physical == chosen[0]); assert_bytes(backend.ram[0], PAGE, 0);
    OK(frame_pool_cancel(&pool, id, OWNER)); assert_all_free(); assert_aliases();
}
static uint32_t random_next(uint32_t *state) {
    *state ^= *state << 13; *state ^= *state >> 17; *state ^= *state << 5;
    return *state;
}
static void test_fragmented_lifecycle_model(void) {
    start();
    struct frame_record model[SMALL_POOL] = {{0}};
    unsigned char patterns[SMALL_POOL] = {0};
    uint64_t generation = 0;
    uint32_t random = 0x47544f53;
    for (unsigned trial=0; trial<1500; ++trial) {
        unsigned operation = random_next(&random)%5;
        unsigned slot = random_next(&random)%SMALL_POOL;
        if (operation < 2) {
            unsigned free_slot = 0;
            while (free_slot < SMALL_POOL && model[free_slot].role != FRAME_FREE) ++free_slot;
            struct frame_id id = {CANARY, CANARY};
            enum frame_role staged = operation ? FRAME_STAGED_TABLE : FRAME_STAGED_DATA;
            uint64_t owner = UINT64_C(0x100000000) + trial;
            if (free_slot == SMALL_POOL) {
                snapshot = pool;
                ERROR(frame_pool_allocate(&pool, staged, owner, &id), FRAME_EXHAUSTED);
                assert(id.physical == CANARY && id.generation == CANARY); assert_unchanged();
            } else {
                OK(frame_pool_allocate(&pool, staged, owner, &id));
                ++generation;
                assert(id.physical == chosen[free_slot] && id.generation == generation);
                assert_bytes(backend.ram[free_slot], PAGE, 0);
                model[free_slot] = (struct frame_record){owner, generation, staged};
                patterns[free_slot] = (unsigned char)(1 + trial%255);
                memset(backend.ram[free_slot], patterns[free_slot], PAGE);
            }
        } else if (operation == 2) {
            enum frame_role old = model[slot].role;
            if (old == FRAME_STAGED_DATA || old == FRAME_STAGED_TABLE) {
                enum frame_role next = old == FRAME_STAGED_DATA ? FRAME_DATA :
                    (enum frame_role)(FRAME_PT + random_next(&random)%3);
                struct frame_id id = {chosen[slot], model[slot].generation};
                OK(frame_pool_promote(&pool, id, model[slot].owner, next));
                model[slot].role = next;
            }
        } else if (operation == 3) {
            enum frame_role old = model[slot].role;
            struct frame_id id = {chosen[slot], model[slot].generation};
            if (old == FRAME_STAGED_DATA || old == FRAME_STAGED_TABLE) {
                OK(frame_pool_cancel(&pool, id, model[slot].owner));
                model[slot].role = FRAME_FREE; model[slot].owner = 0;
            } else if (old == FRAME_DATA || old == FRAME_PT || old == FRAME_PD || old == FRAME_PDPT) {
                OK(frame_pool_retire(&pool, id, model[slot].owner, old));
                model[slot].role = FRAME_RETIRING;
            }
        } else {
            unsigned retired = 0, free_count = 0, flushes = backend.flushes;
            for (unsigned i=0; i<SMALL_POOL; ++i) {
                retired += model[i].role == FRAME_RETIRING;
                free_count += model[i].role == FRAME_FREE;
            }
            backend.expect_retired = retired; backend.expect_free = free_count;
            uint32_t released = UINT32_MAX; OK(frame_pool_reclaim(&pool, &released));
            backend.expect_retired = 0;
            assert(released == retired && backend.flushes == flushes + !!retired);
            for (unsigned i=0; i<SMALL_POOL; ++i)
                if (model[i].role == FRAME_RETIRING) {
                    model[i].role = FRAME_FREE; model[i].owner = 0;
                }
        }
        uint32_t roles[FRAME_ROLE_COUNT] = {0};
        assert(pool.generation == generation);
        for (unsigned i=0; i<SMALL_POOL; ++i) {
            assert(pool.records[i].role == model[i].role);
            assert(pool.records[i].owner == model[i].owner);
            assert(pool.records[i].generation == model[i].generation);
            ++roles[model[i].role];
            if (model[i].generation) assert_bytes(backend.ram[i], PAGE, patterns[i]);
            else assert_bytes(backend.ram[i], PAGE, 0xa5);
        }
        assert_roles(roles);
    }
    /* End in the independently predicted empty ownership state. */
    for (unsigned i=0; i<SMALL_POOL; ++i) {
        struct frame_id id = {chosen[i], model[i].generation};
        enum frame_role old = model[i].role;
        if (old == FRAME_STAGED_DATA || old == FRAME_STAGED_TABLE)
            OK(frame_pool_cancel(&pool, id, model[i].owner));
        else if (old == FRAME_DATA || old == FRAME_PT || old == FRAME_PD || old == FRAME_PDPT)
            OK(frame_pool_retire(&pool, id, model[i].owner, old));
    }
    backend.expect_retired = pool.roles[FRAME_RETIRING];
    backend.expect_free = pool.roles[FRAME_FREE];
    uint32_t released; OK(frame_pool_reclaim(&pool, &released)); backend.expect_retired = 0;
    assert_all_free(); assert_aliases();
}
static void test_malformed_boot_and_init_arguments(void) {
    fixture();
    for (size_t available=0; available<handoff_size; ++available) {
        const char *boot_error = NULL;
        ERROR(frame_pool_init(&pool, handoff, available, &request, &platform, &boot_error), FRAME_BOOT_MEMORY);
        assert(boot_error && !backend.reads && !backend.writes && !backend.flushes && !backend.aliases);
        assert_empty(); assert_baseline();
    }
    for (unsigned invalid=0; invalid<8; ++invalid) {
        fixture();
        switch (invalid) {
        case 0: w32(12, 0); break;
        case 1: w32(8, 18); break; /* EFI boot services are still live */
        case 2: request.min_frames = SMALL_POOL+1; break;
        case 3: request.min_frames = 25; request.max_frames = 25; break;
        case 4: request.original_info_size = handoff_size-8; break;
        case 5: request.retained = NULL; break;
        case 6: retained.end = retained.start; break;
        case 7: request.min_frames = 0; request.max_frames = 0; break; /* default minimum */
        }
        const char *boot_error = NULL;
        ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, &platform, &boot_error), FRAME_BOOT_MEMORY);
        assert(boot_error && !backend.reads && !backend.writes && !backend.flushes && !backend.aliases);
        assert_empty(); assert_baseline();
    }
    fixture();
    ERROR(frame_pool_init(&pool, NULL, handoff_size, &request, &platform, NULL), FRAME_BOOT_MEMORY); assert_empty();
    ERROR(frame_pool_init(&pool, handoff, handoff_size, NULL, &platform, NULL), FRAME_BOOT_MEMORY); assert_empty();
    ERROR(frame_pool_init(NULL, handoff, handoff_size, &request, &platform, NULL), FRAME_BAD_ARGUMENT);
    ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, NULL, NULL), FRAME_BAD_ARGUMENT); assert_empty();
    for (unsigned callback_index=0; callback_index<5; ++callback_index) {
        struct frame_platform broken = platform;
        switch (callback_index) {
        case 0: broken.context_ok = NULL; break;
        case 1: broken.read_leaf = NULL; break;
        case 2: broken.write_leaf = NULL; break;
        case 3: broken.flush = NULL; break;
        case 4: broken.alias = NULL; break;
        }
        ERROR(frame_pool_init(&pool, handoff, handoff_size, &request, &broken, NULL), FRAME_BAD_ARGUMENT); assert_empty();
    }
    assert(!backend.reads && !backend.writes && !backend.flushes && !backend.aliases);
}
int main(void) {
    test_boot_selection_and_aliases();
    test_conflict_preflight_and_rollback();
    test_allocate_exhaust_cancel_reuse();
    test_live_roles_retirement_and_reclaim();
    test_identity_owner_and_arguments();
    test_failure_injection_and_rollback();
    test_context_and_recursion();
    test_metadata_and_leaf_corruption();
    test_generation_exhaustion();
    test_private_selection_lifetime();
    test_fragmented_lifecycle_model();
    test_malformed_boot_and_init_arguments();
    puts("x64 frame pool host tests: PASS (real selector, byte zeroing, ownership, finite roles, rollback, flush-before-free, corruption)");
    return 0;
}

/* Host evidence for the exact guest sparse_vm/frame_pool/boot_memory/handoff
 * cores. Byte-backed RAM and an observed flush callback are mocks, not evidence
 * of hardware page faults, guest RAM, CR3 invalidation, or stale-TLB behavior. */
#include "sparse_vm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEAVES (BOOTINFO_CEILING / VM_PAGE)
#define MODEL_PAGES 1024u
#define NORMAL_FRAMES 384u
#define ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define GIANT UINT64_C(0x158fffff000)
#define BOUNDARY_2M UINT64_C(0x200000)
#define BOUNDARY_1G UINT64_C(0x40000000)
#define BOUNDARY_512G UINT64_C(0x8000000000)
#define OK(call) check_error((call), VM_OK, #call, __LINE__)
#define ERROR(call, expected) check_error((call), (expected), #call, __LINE__)
#define FRAME_OKAY(call) assert((call) == FRAME_OK)

struct model_region { struct vm_region region; unsigned live; };
struct model_page { uint64_t va; enum vm_perm permission; unsigned char pattern; unsigned live; };
struct fixture {
    struct frame_pool pool;
    struct vm_space space;
    _Alignas(4096) uint64_t root[512];
    uint64_t leaves[LEAVES];
    /* uint64_t backing gives page-table aliases the required alignment. */
    uint64_t ram[BOOT_MEMORY_MAX_FRAMES][512];
    struct model_region regions[VM_MAX_REGIONS];
    struct model_page pages[MODEL_PAGES];
    unsigned context, recurse, recursive_checks, flushes, aliases;
    unsigned expected_retiring, free_before_retirement;
    unsigned char retirement_mask[BOOT_MEMORY_MAX_FRAMES];
};
static struct fixture f, other;
static _Alignas(4096) uint64_t unbound_root[512];
static struct {
    struct vm_slot regions[VM_MAX_REGIONS];
    struct vm_backing backing[BOOT_MEMORY_MAX_FRAMES];
    struct frame_record frames[BOOT_MEMORY_MAX_FRAMES];
    struct vm_counts counts;
    uint64_t root[512], ram[BOOT_MEMORY_MAX_FRAMES][512], pool_generation;
    unsigned flushes;
} before;
static unsigned checkpoints, injection_trials, lifecycle_trials, metadata_trials;
static uint64_t lifecycle_visits;

static void check_error(enum vm_error actual, enum vm_error wanted, const char *what, unsigned line) {
    if (actual != wanted) {
        fprintf(stderr, "vm_core_test:%u: %s returned %u, wanted %u\n", line, what,
                (unsigned)actual, (unsigned)wanted);
        abort();
    }
}
static unsigned physical_index(struct fixture *x, uint64_t physical) {
    for (unsigned i = 0; i < x->pool.selection.managed_count; ++i)
        if (x->pool.selection.frames[i] == physical) return i;
    assert(!"page-table walk escaped the independently selected pool");
    return 0;
}
static int host_context(void *opaque) {
    struct fixture *x = opaque;
    if (x->recurse && x->space.ready) {
        struct vm_counts out;
        ERROR(vm_stats(&x->space, &out), VM_STATE);
        ++x->recursive_checks;
    }
    return x->context && (!x->space.ready || vm_owned_hierarchy_valid(&x->space));
}
static uint64_t host_read(void *opaque, uint64_t physical) {
    struct fixture *x = opaque;
    assert(!(physical & (VM_PAGE-1)) && physical < BOOTINFO_CEILING);
    return x->leaves[physical / VM_PAGE];
}
static void host_write(void *opaque, uint64_t physical, uint64_t entry) {
    struct fixture *x = opaque;
    assert(!(physical & (VM_PAGE-1)) && physical < BOOTINFO_CEILING);
    assert(!entry || entry == (physical | FRAME_POOL_NX | 3));
    assert(!entry || !x->leaves[physical / VM_PAGE]);
    x->leaves[physical / VM_PAGE] = entry;
}
/* Walk actual links, independent of the core's backing-to-path lookup. Every
 * flush must precede reuse, and no retiring frame may remain reachable. */
static void callback_walk(struct fixture *x, uint64_t physical, unsigned level) {
    unsigned index = physical_index(x, physical);
    assert(x->pool.records[index].role != FRAME_RETIRING);
    assert(x->pool.records[index].role != FRAME_FREE);
    if (!level) return;
    for (unsigned i = 0; i < 512; ++i) {
        uint64_t entry = x->ram[index][i];
        if (entry) callback_walk(x, entry & ADDRESS_MASK, level-1);
    }
}
static void host_flush(void *opaque) {
    struct fixture *x = opaque;
    /* Reclamation must reach this callback before any retired ID becomes FREE.
     * A structural snapshot below separately detects skipped publication flushes. */
    for (unsigned i = 0; i < x->pool.selection.managed_count; ++i)
        if (x->pool.records[i].role == FRAME_RETIRING)
            assert(x->pool.records[i].owner && x->pool.records[i].generation);
    if (x->expected_retiring) {
        assert(x->pool.roles[FRAME_RETIRING]);
        assert(x->pool.roles[FRAME_FREE] == x->free_before_retirement);
        x->free_before_retirement += x->pool.roles[FRAME_RETIRING];
        for (unsigned i = 0; i < x->pool.selection.managed_count; ++i) {
            if (x->retirement_mask[i] == 1) {
                assert(x->pool.records[i].role == FRAME_DATA || x->pool.records[i].role == FRAME_RETIRING);
                if (x->pool.records[i].role == FRAME_RETIRING) x->retirement_mask[i] = 2;
            } else if (x->retirement_mask[i] == 2) assert(x->pool.records[i].role == FRAME_FREE);
        }
    }
    if (x->space.ready)
        for (unsigned i = 288; i < 304; ++i)
            if (x->root[i]) callback_walk(x, x->root[i] & ADDRESS_MASK, 3);
    ++x->flushes;
}
static volatile unsigned char *host_alias(void *opaque, uint64_t physical) {
    struct fixture *x = opaque;
    assert(x->leaves[physical / VM_PAGE] == (physical | FRAME_POOL_NX | 3));
    assert(x->flushes);
    ++x->aliases;
    return (volatile unsigned char *)x->ram[physical_index(x, physical)];
}
static void w32(unsigned char *bytes, unsigned at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at+i] = (unsigned char)(value >> (8*i));
}
static void w64(unsigned char *bytes, unsigned at, uint64_t value) {
    w32(bytes, at, (uint32_t)value); w32(bytes, at+4, (uint32_t)(value >> 32));
}
static void start(struct fixture *x, unsigned count) {
    unsigned char handoff[80] = {0};
    memset(x, 0, sizeof(*x));
    memset(x->ram, 0xa5, sizeof(x->ram));
    x->context = 1;
    /* The real validated parser selects from this type-1 map. An excluded,
     * non-page-aligned kernel and loader copy force inward/outward rounding. */
    w32(handoff, 0, 80); w32(handoff, 8, 6); w32(handoff, 12, 64);
    w32(handoff, 16, 24);
    w64(handoff, 24, 0x100000); w64(handoff, 32, 0xf00000); w32(handoff, 40, 1);
    w64(handoff, 48, 0x1000000); w64(handoff, 56, 0x100000); w32(handoff, 64, 2);
    w32(handoff, 72, 0); w32(handoff, 76, 8);
    struct boot_memory_request request = {
        .kernel_start = 0x100000, .kernel_end = 0x180123,
        .original_info_start = 0x183020, .original_info_size = sizeof(handoff),
        .min_frames = 1, .max_frames = count
    };
    struct frame_platform platform = {host_context, host_read, host_write, host_flush, host_alias, x};
    const char *error = NULL;
    FRAME_OKAY(frame_pool_init(&x->pool, handoff, sizeof(handoff), &request, &platform, &error));
    assert(!error && x->pool.selection.managed_count == count);
    assert(x->pool.selection.frames[0] == 0x181000);
    /* A borrowed lower-half root entry must never be interpreted or modified. */
    x->root[0] = UINT64_C(0x12345003);
    ERROR(vm_init(&x->space, &x->pool, x->root+1), VM_ARGUMENT);
    ERROR(vm_init(&x->space, &x->pool, x->root), VM_STATE);
    x->pool.service_root = x->root;
    ERROR(vm_init(&x->space, &x->pool, unbound_root), VM_STATE);
    assert(!x->space.ready && !x->pool.service_owner);
    OK(vm_init(&x->space, &x->pool, x->root));
    assert(x->space.space_id && x->space.ready && !x->space.busy);
}
static int same_handle(struct vm_handle a, struct vm_handle b) {
    return a.space_id == b.space_id && a.generation == b.generation && a.slot == b.slot;
}
static int page_find(struct fixture *x, uint64_t va) {
    for (unsigned i = 0; i < MODEL_PAGES; ++i)
        if (x->pages[i].live && x->pages[i].va == va) return (int)i;
    return -1;
}
static struct vm_region reserve_at(struct fixture *x, uint64_t bytes, uint64_t alignment,
                                   enum vm_placement placement, uint64_t exact) {
    struct vm_region r;
    OK(vm_reserve(&x->space, bytes, alignment, placement, exact, &r));
    assert(r.length == bytes && !(r.base & (alignment-1)));
    assert(r.base >= VM_ARENA_START && r.length <= VM_ARENA_END-r.base);
    if (placement == VM_EXACT) assert(r.base == exact);
    unsigned free_slot = VM_MAX_REGIONS;
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) {
        if (!x->regions[i].live) { free_slot = i; continue; }
        struct vm_region s = x->regions[i].region;
        assert(r.base+r.length <= s.base || s.base+s.length <= r.base);
        assert(!same_handle(r.handle, s.handle));
    }
    assert(free_slot < VM_MAX_REGIONS);
    x->regions[free_slot] = (struct model_region){r, 1};
    return r;
}
static void model_commit(struct fixture *x, struct vm_region r, uint64_t off,
                         unsigned pages, enum vm_perm permission) {
    for (unsigned p = 0; p < pages; ++p) {
        uint64_t va = r.base+off+(uint64_t)p*VM_PAGE;
        int found = page_find(x, va);
        if (found < 0) {
            unsigned i = 0;
            while (i < MODEL_PAGES && x->pages[i].live) ++i;
            assert(i < MODEL_PAGES);
            x->pages[i] = (struct model_page){va, permission, 0, 1};
        } else x->pages[found].permission = permission;
    }
}
static void commit(struct fixture *x, struct vm_region r, uint64_t off,
                   unsigned pages, enum vm_perm permission) {
    unsigned prior_flushes = x->flushes;
    OK(vm_commit(&x->space, r.handle, off, (uint64_t)pages*VM_PAGE, permission));
    assert(x->flushes > prior_flushes);
    model_commit(x, r, off, pages, permission);
}
static void model_protect(struct fixture *x, struct vm_region r, uint64_t off,
                          uint64_t bytes, enum vm_perm permission) {
    for (unsigned i = 0; i < MODEL_PAGES; ++i)
        if (x->pages[i].live && x->pages[i].va >= r.base+off &&
            x->pages[i].va-r.base-off < bytes) x->pages[i].permission = permission;
}
static void protect(struct fixture *x, struct vm_region r, uint64_t off,
                    uint64_t bytes, enum vm_perm permission) {
    unsigned changed = 0, prior_flushes = x->flushes;
    for (unsigned i = 0; i < MODEL_PAGES; ++i)
        if (x->pages[i].live && x->pages[i].va >= r.base+off &&
            x->pages[i].va-r.base-off < bytes && x->pages[i].permission != permission) ++changed;
    OK(vm_protect(&x->space, r.handle, off, bytes, permission));
    if (changed) assert(x->flushes > prior_flushes);
    model_protect(x, r, off, bytes, permission);
}
static void model_decommit(struct fixture *x, struct vm_region r, uint64_t off, uint64_t bytes) {
    for (unsigned i = 0; i < MODEL_PAGES; ++i)
        if (x->pages[i].live && x->pages[i].va >= r.base+off &&
            x->pages[i].va-r.base-off < bytes) x->pages[i].live = 0;
}
static void expect_retirement(struct fixture *x, struct vm_region r, uint64_t off, uint64_t bytes) {
    x->expected_retiring = 0;
    memset(x->retirement_mask, 0, sizeof(x->retirement_mask));
    x->free_before_retirement = x->pool.roles[FRAME_FREE];
    for (unsigned i = 0; i < MODEL_PAGES; ++i)
        if (x->pages[i].live && x->pages[i].va >= r.base+off &&
            x->pages[i].va-r.base-off < bytes) {
            struct vm_page_state state;
            OK(vm_query(&x->space, r.handle, x->pages[i].va-r.base, &state));
            assert(state.backed);
            x->retirement_mask[physical_index(x, state.frame.physical)] = 1;
            ++x->expected_retiring;
        }
}
static void retirement_complete(struct fixture *x) {
    for (unsigned i = 0; i < x->pool.selection.managed_count; ++i)
        if (x->retirement_mask[i]) assert(x->retirement_mask[i] == 2);
    x->expected_retiring = 0;
}
static void decommit(struct fixture *x, struct vm_region r, uint64_t off, uint64_t bytes) {
    unsigned prior_flushes = x->flushes;
    expect_retirement(x, r, off, bytes);
    OK(vm_decommit(&x->space, r.handle, off, bytes));
    if (x->expected_retiring) assert(x->flushes > prior_flushes);
    retirement_complete(x);
    model_decommit(x, r, off, bytes);
}
static void release(struct fixture *x, struct vm_region r) {
    unsigned prior_flushes = x->flushes;
    expect_retirement(x, r, 0, r.length);
    OK(vm_release(&x->space, r.handle));
    if (x->expected_retiring) assert(x->flushes > prior_flushes);
    retirement_complete(x);
    model_decommit(x, r, 0, r.length);
    unsigned found = 0;
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i)
        if (x->regions[i].live && same_handle(x->regions[i].region.handle, r.handle)) {
            x->regions[i].live = 0; ++found;
        }
    assert(found == 1);
}
static void paint(struct fixture *x, struct vm_region r, uint64_t off, unsigned char pattern) {
    struct vm_page_state state;
    OK(vm_query(&x->space, r.handle, off, &state));
    assert(state.backed);
    memset(x->ram[physical_index(x, state.frame.physical)], pattern, VM_PAGE);
    int i = page_find(x, r.base+off); assert(i >= 0);
    x->pages[i].pattern = pattern;
}
static unsigned distinct_prefixes(struct fixture *x, unsigned shift) {
    unsigned count = 0;
    for (unsigned i = 0; i < MODEL_PAGES; ++i) if (x->pages[i].live) {
        unsigned prior = 0;
        for (unsigned j = 0; j < i; ++j)
            if (x->pages[j].live && (x->pages[j].va >> shift) == (x->pages[i].va >> shift))
                prior = 1;
        if (!prior) ++count;
    }
    return count;
}
static void check_query(struct fixture *x, struct vm_region r, uint64_t off) {
    struct vm_page_state state;
    memset(&state, 0xcc, sizeof(state));
    OK(vm_query(&x->space, r.handle, off, &state));
    int i = page_find(x, r.base+off);
    assert(state.backed == (unsigned)(i >= 0));
    if (i < 0) { assert(state.permission == VM_NONE); return; }
    struct model_page *p = &x->pages[i];
    assert(state.permission == p->permission);
    unsigned index = physical_index(x, state.frame.physical);
    assert(x->pool.records[index].role == FRAME_DATA);
    assert(x->pool.records[index].generation == state.frame.generation);
    const unsigned char *bytes = (const unsigned char *)x->ram[index];
    for (unsigned j = 0; j < VM_PAGE; ++j) assert(bytes[j] == p->pattern);
}
static void walk_table(struct fixture *x, uint64_t physical, unsigned level,
                       uint64_t base, unsigned *counts, unsigned char *seen) {
    unsigned index = physical_index(x, physical);
    assert(!seen[index]); seen[index] = 1;
    enum frame_role expected = level == 3 ? FRAME_PDPT : level == 2 ? FRAME_PD : FRAME_PT;
    assert(x->pool.records[index].role == expected);
    ++counts[level];
    const uint64_t *table = x->ram[index];
    unsigned shift = level == 3 ? 30 : level == 2 ? 21 : 12;
    for (unsigned j = 0; j < 512; ++j) {
        uint64_t entry = table[j], va = base+((uint64_t)j << shift);
        if (level != 1) {
            if (!entry) continue;
            assert((entry & (UINT64_C(0xfff) & ~UINT64_C(0x20))) == 3);
            assert(!(entry & ~(ADDRESS_MASK | FRAME_POOL_NX | UINT64_C(0x23))));
            walk_table(x, entry & ADDRESS_MASK, level-1, va, counts, seen);
        } else {
            int model = page_find(x, va);
            if (model < 0 || x->pages[model].permission == VM_NONE) { assert(!entry); continue; }
            unsigned data = physical_index(x, entry & ADDRESS_MASK);
            assert(!seen[data]); seen[data] = 1;
            uint64_t flags = FRAME_POOL_NX | (x->pages[model].permission == VM_READ_WRITE ? 3 : 1);
            assert((entry & ~UINT64_C(0x60)) == (x->pool.selection.frames[data] | flags));
            assert(x->pool.records[data].role == FRAME_DATA);
            ++counts[0];
        }
    }
}
static void audit(struct fixture *x) {
    struct vm_counts got;
    OK(vm_stats(&x->space, &got)); OK(vm_audit(&x->space));
    FRAME_OKAY(frame_pool_audit(&x->pool));
    assert(vm_owned_hierarchy_valid(&x->space));
    uint64_t reserved = 0; unsigned reservations = 0, data = 0, none = 0;
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) if (x->regions[i].live) {
        struct vm_region r = x->regions[i].region;
        ++reservations; reserved += r.length;
        check_query(x, r, 0); check_query(x, r, r.length-VM_PAGE);
        for (unsigned p = 0; p < MODEL_PAGES; ++p)
            if (x->pages[p].live && x->pages[p].va >= r.base && x->pages[p].va-r.base < r.length)
                check_query(x, r, x->pages[p].va-r.base);
    }
    for (unsigned i = 0; i < MODEL_PAGES; ++i) if (x->pages[i].live) {
        ++data; none += x->pages[i].permission == VM_NONE;
    }
    unsigned pt = distinct_prefixes(x, 21), pd = distinct_prefixes(x, 30), pdpt = distinct_prefixes(x, 39);
    assert(got.reservations == reservations && got.reserved_bytes == reserved);
    assert(got.data == data && got.resident_bytes == (uint64_t)data*VM_PAGE);
    assert(got.resident_none_bytes == (uint64_t)none*VM_PAGE);
    assert(got.accessible_bytes == (uint64_t)(data-none)*VM_PAGE);
    assert(got.pt == pt && got.pd == pd && got.pdpt == pdpt);
    assert(got.managed == x->pool.selection.managed_count);
    assert(got.free_frames+data+pt+pd+pdpt == got.managed);
    assert(got.metadata_bytes >= sizeof(struct vm_space));
    assert(!x->space.busy && !x->space.staged_count && !x->pool.busy);
    assert(!x->pool.roles[FRAME_STAGED_DATA] && !x->pool.roles[FRAME_STAGED_TABLE] && !x->pool.roles[FRAME_RETIRING]);
    assert(x->pool.roles[FRAME_FREE] == got.free_frames);
    assert(x->root[0] == UINT64_C(0x12345003));
    unsigned counts[4] = {0}; unsigned char seen[BOOT_MEMORY_MAX_FRAMES] = {0};
    for (unsigned i = 1; i < 512; ++i) {
        if (i < ((VM_ARENA_START >> 39) & 511) || i >= ((VM_ARENA_END >> 39) & 511)) {
            assert(!x->root[i]); continue;
        }
        uint64_t entry = x->root[i]; if (!entry) continue;
        assert((entry & (UINT64_C(0xfff) & ~UINT64_C(0x20))) == 3);
        assert(!(entry & ~(ADDRESS_MASK | FRAME_POOL_NX | UINT64_C(0x23))));
        walk_table(x, entry & ADDRESS_MASK, 3, UINT64_C(0xffff000000000000) | ((uint64_t)i << 39), counts, seen);
    }
    assert(counts[0] == data-none && counts[1] == pt && counts[2] == pd && counts[3] == pdpt);
    ++checkpoints;
}
static void snapshot(struct fixture *x) {
    memcpy(before.regions, x->space.regions, sizeof(before.regions));
    memcpy(before.backing, x->space.backing, sizeof(before.backing));
    memcpy(before.frames, x->pool.records, sizeof(before.frames));
    memcpy(before.root, x->root, sizeof(before.root));
    for (unsigned i = 0; i < x->pool.selection.managed_count; ++i)
        if (x->pool.records[i].role != FRAME_FREE) memcpy(before.ram[i], x->ram[i], VM_PAGE);
    OK(vm_stats(&x->space, &before.counts));
    before.pool_generation = x->pool.generation; before.flushes = x->flushes;
}
static void unchanged(struct fixture *x) {
    struct vm_counts after;
    OK(vm_stats(&x->space, &after));
    assert(!memcmp(before.regions, x->space.regions, sizeof(before.regions)));
    assert(!memcmp(before.backing, x->space.backing, sizeof(before.backing)));
    assert(!memcmp(before.root, x->root, sizeof(before.root)));
    for (unsigned i = 0; i < x->pool.selection.managed_count; ++i) {
        struct frame_record a = before.frames[i], b = x->pool.records[i];
        assert(a.owner == b.owner && a.role == b.role);
        if (a.role != FRAME_FREE) {
            assert(a.generation == b.generation);
            assert(!memcmp(before.ram[i], x->ram[i], VM_PAGE));
        } else assert(b.generation >= a.generation);
    }
    assert(x->pool.generation >= before.pool_generation);
    assert(after.operation_visits >= before.counts.operation_visits);
    assert(after.peak_staged >= before.counts.peak_staged);
    assert(after.flush_epoch == before.counts.flush_epoch && x->flushes == before.flushes);
    after.operation_visits = before.counts.operation_visits;
    after.peak_staged = before.counts.peak_staged;
    assert(!memcmp(&after, &before.counts, sizeof(after)));
    audit(x);
}
static void expect_reserve_error(uint64_t bytes, uint64_t alignment, enum vm_placement placement,
                                 uint64_t exact, enum vm_error expected) {
    struct vm_region result, poison;
    memset(&result, 0xcc, sizeof(result)); memcpy(&poison, &result, sizeof(poison));
    snapshot(&f);
    ERROR(vm_reserve(&f.space, bytes, alignment, placement, exact, &result), expected);
    assert(!memcmp(&result, &poison, sizeof(result))); unchanged(&f);
}

static void test_arguments_ownership_and_slots(void) {
    start(&f, NORMAL_FRAMES); start(&other, 16);
    assert(f.space.space_id != other.space.space_id);
    expect_reserve_error(0, VM_PAGE, VM_ANYWHERE, 0, VM_ARGUMENT);
    expect_reserve_error(1, VM_PAGE, VM_ANYWHERE, 0, VM_ALIGNMENT);
    expect_reserve_error(VM_PAGE, 0, VM_ANYWHERE, 0, VM_ALIGNMENT);
    expect_reserve_error(VM_PAGE, 3*VM_PAGE, VM_ANYWHERE, 0, VM_ALIGNMENT);
    expect_reserve_error(VM_PAGE, 1, VM_ANYWHERE, 0, VM_ALIGNMENT);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START+1, VM_ALIGNMENT);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START-VM_PAGE, VM_RANGE);
    expect_reserve_error(2*VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_END-VM_PAGE, VM_RANGE);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_EXACT, UINT64_C(0x0000800000000000), VM_RANGE);
    expect_reserve_error(UINT64_C(0xfffffffffffff000), VM_PAGE, VM_EXACT, VM_ARENA_START, VM_RANGE);
    expect_reserve_error(VM_PAGE, UINT64_C(1)<<63, VM_ANYWHERE, 0, VM_RANGE);
    expect_reserve_error(VM_PAGE, VM_PAGE, (enum vm_placement)99, 0, VM_UNSUPPORTED);
    expect_reserve_error(UINT64_C(0xffff800000001000), VM_PAGE, VM_EXACT,
                         UINT64_C(0x00007ffffffff000), VM_RANGE);
    struct vm_region entire = reserve_at(&f, VM_ARENA_END-VM_ARENA_START,
                                         VM_PAGE, VM_EXACT, VM_ARENA_START);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_ANYWHERE, 0, VM_CONFLICT);
    release(&f, entire);
    struct vm_region r = reserve_at(&f, 8*VM_PAGE, 65536, VM_ANYWHERE, 0);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_EXACT, r.base, VM_CONFLICT);
    expect_reserve_error(2*VM_PAGE, VM_PAGE, VM_EXACT, r.base+r.length-VM_PAGE, VM_CONFLICT);
    struct vm_region foreign = reserve_at(&other, VM_PAGE, VM_PAGE, VM_EXACT, r.base);
    snapshot(&f);
    ERROR(vm_commit(&f.space, foreign.handle, 0, VM_PAGE, VM_READ_WRITE), VM_FOREIGN);
    struct vm_handle bad = r.handle; bad.generation++;
    ERROR(vm_commit(&f.space, bad, 0, VM_PAGE, VM_READ_WRITE), VM_STALE);
    bad = r.handle; bad.slot = VM_MAX_REGIONS;
    ERROR(vm_release(&f.space, bad), VM_STALE);
    ERROR(vm_commit(&f.space, r.handle, 0, 0, VM_READ_WRITE), VM_ARGUMENT);
    ERROR(vm_commit(&f.space, r.handle, 1, VM_PAGE, VM_READ_WRITE), VM_ALIGNMENT);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE+1, VM_READ_WRITE), VM_ALIGNMENT);
    ERROR(vm_commit(&f.space, r.handle, r.length, VM_PAGE, VM_READ_WRITE), VM_RANGE);
    ERROR(vm_commit(&f.space, r.handle, UINT64_MAX-VM_PAGE+1, VM_PAGE, VM_READ_WRITE), VM_RANGE);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE, VM_NONE), VM_UNSUPPORTED);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE, (enum vm_perm)99), VM_UNSUPPORTED);
    ERROR(vm_protect(&f.space, r.handle, 0, VM_PAGE, VM_READ), VM_UNBACKED);
    struct vm_page_state state, poison; memset(&state, 0xcc, sizeof(state)); poison = state;
    ERROR(vm_query(&f.space, r.handle, r.length, &state), VM_RANGE);
    assert(!memcmp(&state, &poison, sizeof(state))); unchanged(&f);
    release(&f, r); snapshot(&f);
    ERROR(vm_release(&f.space, r.handle), VM_STALE);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE, VM_READ_WRITE), VM_STALE); unchanged(&f);
    struct vm_region reused = reserve_at(&f, r.length, VM_PAGE, VM_EXACT, r.base);
    assert(!same_handle(r.handle, reused.handle)); release(&f, reused); release(&other, foreign);
    struct vm_region all[VM_MAX_REGIONS];
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i)
        all[i] = reserve_at(&f, VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_ANYWHERE, 0, VM_LIMIT);
    release(&f, all[17]);
    struct vm_region hole = reserve_at(&f, VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    assert(hole.base == all[17].base && !same_handle(hole.handle, all[17].handle));
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) if (i != 17) release(&f, all[i]);
    release(&f, hole);
    audit(&f); audit(&other);
    puts("VM_HOST_ARGUMENTS_OWNERSHIP_SLOTS_PASS");
}
static void test_giant_preservation_and_shared_paths(void) {
    start(&f, NORMAL_FRAMES);
    struct vm_region r = reserve_at(&f, GIANT, VM_PAGE, VM_EXACT, VM_ARENA_START);
    audit(&f); assert(!f.pool.generation);
    commit(&f, r, 0, 64, VM_READ_WRITE);
    commit(&f, r, UINT64_C(0xac00000000), 1, VM_READ_WRITE);
    commit(&f, r, UINT64_C(0x157fffff000), 1, VM_READ);
    paint(&f, r, 0, 0x5a); paint(&f, r, UINT64_C(0xac00000000), 0x96);
    paint(&f, r, UINT64_C(0x157fffff000), 0xe3); audit(&f);
    struct vm_counts counts; OK(vm_stats(&f.space, &counts));
    assert(counts.data == 66 && counts.pt+counts.pd+counts.pdpt == 9);
    unsigned flushes = f.flushes;
    protect(&f, r, 0, 64*VM_PAGE, VM_READ); assert(f.flushes > flushes); audit(&f);
    protect(&f, r, 0, 64*VM_PAGE, VM_NONE); audit(&f);
    commit(&f, r, 0, 65, VM_READ_WRITE); audit(&f); /* Existing bytes survive, new page zero. */
    snapshot(&f);
    ERROR(vm_protect(&f.space, r.handle, 0, 66*VM_PAGE, VM_READ), VM_UNBACKED);
    unchanged(&f);
    protect(&f, r, 0, GIANT, VM_NONE); audit(&f);
    decommit(&f, r, 0, 65*VM_PAGE); audit(&f);
    commit(&f, r, 0, 64, VM_READ_WRITE); audit(&f); /* All reclaimed bytes must be zero. */
    uint64_t visits = f.space.operation_visits;
    release(&f, r); assert(f.space.operation_visits-visits < UINT64_C(1000000)); audit(&f);
    r = reserve_at(&f, GIANT, VM_PAGE, VM_EXACT, VM_ARENA_START);
    commit(&f, r, UINT64_C(0x1000000000), 64, VM_READ_WRITE); audit(&f);
    OK(vm_stats(&f.space, &counts)); assert(counts.data == 64 && counts.pt+counts.pd+counts.pdpt == 3);
    release(&f, r);
    struct vm_region a = reserve_at(&f, VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START);
    struct vm_region b = reserve_at(&f, VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START+VM_PAGE);
    commit(&f, a, 0, 1, VM_READ_WRITE); commit(&f, b, 0, 1, VM_READ_WRITE);
    paint(&f, b, 0, 0x73); protect(&f, b, 0, VM_PAGE, VM_NONE); audit(&f);
    release(&f, a); audit(&f); assert(f.pool.roles[FRAME_PT] == 1);
    protect(&f, b, 0, VM_PAGE, VM_READ_WRITE); audit(&f);
    release(&f, b); audit(&f);
    puts("VM_HOST_GIANT_PRESERVE_SHARED_PASS");
}
static void test_boundaries_and_limits(void) {
    const uint64_t boundaries[] = {BOUNDARY_2M, BOUNDARY_1G, BOUNDARY_512G};
    for (unsigned b = 0; b < 3; ++b) {
        start(&f, NORMAL_FRAMES);
        struct vm_region r = reserve_at(&f, 4*VM_PAGE, VM_PAGE, VM_EXACT,
                                       VM_ARENA_START+boundaries[b]-2*VM_PAGE);
        commit(&f, r, VM_PAGE, 2, VM_READ_WRITE); audit(&f);
        paint(&f, r, VM_PAGE, 0x17); paint(&f, r, 2*VM_PAGE, 0xcb);
        protect(&f, r, 0, r.length, VM_NONE); audit(&f);
        decommit(&f, r, VM_PAGE, VM_PAGE); audit(&f);
        commit(&f, r, VM_PAGE, 2, VM_READ); audit(&f);
        release(&f, r); audit(&f);
    }
    start(&f, NORMAL_FRAMES);
    struct vm_region r = reserve_at(&f, (VM_MAX_COMMIT_PAGES+1)*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    snapshot(&f);
    ERROR(vm_commit(&f.space, r.handle, 0, r.length, VM_READ_WRITE), VM_LIMIT); unchanged(&f);
    release(&f, r);
    start(&f, 7);
    r = reserve_at(&f, 8*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    commit(&f, r, 0, 1, VM_READ_WRITE); paint(&f, r, 0, 0x39); audit(&f);
    snapshot(&f);
    ERROR(vm_commit(&f.space, r.handle, 0, 5*VM_PAGE, VM_READ), VM_NO_FRAMES); unchanged(&f);
    commit(&f, r, VM_PAGE, 3, VM_READ_WRITE); audit(&f); assert(!f.pool.roles[FRAME_FREE]);
    release(&f, r); audit(&f);
    puts("VM_HOST_BOUNDARIES_LIMITS_OOM_PASS");
}
struct failure_case { uint64_t offset; unsigned pages, existing, allocations; };
static void test_allocation_failures(void) {
    /* Fresh paths, extant PT, missing PT/PD/PDPT, and the exact maximum 262-frame
     * transaction across a PML4 boundary. Every actual allocation index fails. */
    const struct failure_case cases[] = {
        {0, 2, 0, 5}, {0, 3, 1, 2},
        {BOUNDARY_2M-VM_PAGE, 2, 1, 2},
        {BOUNDARY_1G-VM_PAGE, 2, 1, 3},
        {BOUNDARY_512G-VM_PAGE, 2, 1, 4},
        {BOUNDARY_2M-VM_PAGE, 2, 0, 6},
        {BOUNDARY_1G-VM_PAGE, 2, 0, 7},
        {BOUNDARY_512G-VM_PAGE, 2, 0, 8},
        {BOUNDARY_512G-128*VM_PAGE, VM_MAX_COMMIT_PAGES, 0, VM_MAX_STAGED}
    };
    for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); ++c) {
        const struct failure_case *test = &cases[c];
        for (unsigned nth = 1; nth <= test->allocations; ++nth) {
            start(&f, NORMAL_FRAMES);
            struct vm_region r = reserve_at(&f, GIANT, VM_PAGE, VM_EXACT, VM_ARENA_START);
            if (test->existing) {
                commit(&f, r, test->offset, 1, VM_READ_WRITE);
                paint(&f, r, test->offset, (unsigned char)(0x40+c));
            }
            FRAME_OKAY(frame_pool_fail_after(&f.pool, nth)); snapshot(&f);
            ERROR(vm_commit(&f.space, r.handle, test->offset,
                            (uint64_t)test->pages*VM_PAGE, VM_READ), VM_INJECTED);
            assert(f.pool.allocation_attempt == nth);
            assert(f.space.peak_staged >= nth-1); /* A failed transaction keeps its diagnostic peak. */
            assert(f.pool.generation == before.pool_generation+nth-1);
            unchanged(&f); ++injection_trials;
            FRAME_OKAY(frame_pool_fail_after(&f.pool, 0));
            commit(&f, r, test->offset, test->pages, VM_READ); audit(&f);
            release(&f, r); audit(&f);
        }
    }
    printf("VM_HOST_EVERY_ALLOCATION_FAILURE_PASS trials=%u\n", injection_trials);
}
static uint32_t random_state = UINT32_C(0x61375a2b);
static uint32_t random_next(void) {
    uint32_t x = random_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return random_state = x;
}
static void test_fragmented_model(void) {
    start(&f, NORMAL_FRAMES);
    struct vm_region r[24]; unsigned live[24] = {0};
    for (unsigned step = 0; step < 360; ++step) {
        unsigned index = random_next()%24, action = random_next()%6;
        if (!live[index]) {
            uint64_t chunk = (uint64_t)(index/8)*BOUNDARY_512G;
            uint64_t base = VM_ARENA_START+chunk+(uint64_t)(index%8)*BOUNDARY_2M;
            r[index] = reserve_at(&f, 12*VM_PAGE, VM_PAGE, VM_EXACT, base); live[index] = 1;
        } else {
            unsigned first = random_next()%12, pages = 1+random_next()%(12-first);
            uint64_t off = (uint64_t)first*VM_PAGE, bytes = (uint64_t)pages*VM_PAGE;
            if (action <= 1) {
                commit(&f, r[index], off, pages, action ? VM_READ : VM_READ_WRITE);
                paint(&f, r[index], off, (unsigned char)(1+step%254));
            } else if (action == 2) protect(&f, r[index], off, bytes, VM_NONE);
            else if (action == 3) decommit(&f, r[index], off, bytes);
            else if (action == 4) { release(&f, r[index]); live[index] = 0; }
            else {
                unsigned missing = 0;
                for (unsigned p = 0; p < pages; ++p)
                    missing += page_find(&f, r[index].base+off+(uint64_t)p*VM_PAGE) < 0;
                if (missing) {
                    snapshot(&f);
                    ERROR(vm_protect(&f.space, r[index].handle, off, bytes, VM_READ_WRITE), VM_UNBACKED);
                    unchanged(&f);
                } else protect(&f, r[index], off, bytes, VM_READ_WRITE);
            }
        }
        audit(&f);
    }
    for (unsigned i = 0; i < 24; ++i) if (live[i]) release(&f, r[i]);
    audit(&f); puts("VM_HOST_FRAGMENTED_ORACLE_PASS seed=0x61375a2b operations=360");
}
static void test_corruption_detection(void) {
    start(&f, NORMAL_FRAMES);
    struct vm_region r = reserve_at(&f, 2*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    commit(&f, r, 0, 1, VM_READ_WRITE); audit(&f);
    uint64_t root = f.root[288];
    unsigned aliases = f.aliases;
    f.root[288] = UINT64_C(0xfeed000) | FRAME_POOL_NX | 3;
    assert(!vm_owned_hierarchy_valid(&f.space));
    assert(f.aliases == aliases); /* Reject an unowned child before obtaining an alias. */
    f.root[288] = root;
    uint64_t *pdpt = f.ram[physical_index(&f, root & ADDRESS_MASK)];
    uint64_t *pd = f.ram[physical_index(&f, pdpt[0] & ADDRESS_MASK)];
    uint64_t *pt = f.ram[physical_index(&f, pd[0] & ADDRESS_MASK)];
    uint64_t entry = pt[0];
    pt[0] = 0; /* Missing live leaf must be found by backing-to-hierarchy audit. */
    ERROR(vm_audit(&f.space), VM_CORRUPT);
    pt[0] = entry | 4; assert(!vm_owned_hierarchy_valid(&f.space));
    pt[0] = entry;
    ++f.pool.roles[FRAME_DATA]; ERROR(vm_audit(&f.space), VM_CORRUPT); --f.pool.roles[FRAME_DATA];
    uint64_t length = f.space.regions[r.handle.slot].length;
    f.space.regions[r.handle.slot].length = 0;
    ERROR(vm_audit(&f.space), VM_CORRUPT);
    f.space.regions[r.handle.slot].length = length;
    audit(&f); release(&f, r); audit(&f);
    puts("VM_HOST_CORRUPTION_DETECTION_PASS");
}
static void test_nonwrapping_and_context(void) {
    start(&f, NORMAL_FRAMES);
    /* White-box exhaustion setup, followed by public operations. No live token
     * is forged: this forces the next never-live lifetime to its maximum. */
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) f.space.regions[i].generation = UINT64_MAX;
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_ANYWHERE, 0, VM_LIMIT);
    f.space.regions[7].generation = UINT64_MAX-1;
    struct vm_region last = reserve_at(&f, VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    assert(last.handle.slot == 7 && last.handle.generation == UINT64_MAX);
    release(&f, last);
    ERROR(vm_release(&f.space, last.handle), VM_STALE);
    expect_reserve_error(VM_PAGE, VM_PAGE, VM_ANYWHERE, 0, VM_LIMIT);
    start(&f, NORMAL_FRAMES);
    struct vm_region r = reserve_at(&f, 2*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    f.pool.generation = UINT64_MAX-1;
    snapshot(&f);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE, VM_READ_WRITE), VM_ID_EXHAUSTED);
    unchanged(&f); assert(f.pool.generation == UINT64_MAX-1);
    f.pool.generation = UINT64_MAX;
    snapshot(&f);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE, VM_READ_WRITE), VM_ID_EXHAUSTED); unchanged(&f);
    release(&f, r);
    start(&f, NORMAL_FRAMES);
    r = reserve_at(&f, 2*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    f.pool.generation = UINT64_MAX-4;
    commit(&f, r, 0, 1, VM_READ_WRITE); assert(f.pool.generation == UINT64_MAX);
    decommit(&f, r, 0, VM_PAGE); snapshot(&f);
    ERROR(vm_commit(&f.space, r.handle, 0, VM_PAGE, VM_READ), VM_ID_EXHAUSTED); unchanged(&f);
    release(&f, r);
    start(&f, NORMAL_FRAMES);
    r = reserve_at(&f, 2*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    f.recurse = 1; commit(&f, r, 0, 1, VM_READ_WRITE); f.recurse = 0;
    assert(f.recursive_checks); audit(&f);
    snapshot(&f); f.context = 0;
    ERROR(vm_commit(&f.space, r.handle, VM_PAGE, VM_PAGE, VM_READ), VM_STATE);
    f.context = 1; unchanged(&f);
    release(&f, r); audit(&f);
    puts("VM_HOST_NONWRAPPING_CONTEXT_PASS");
}
/* Lifecycle oracle: intervals and retained bytes live only in this host model.
 * Geometry is calculated from the request, never reconstructed from VM slots. */
enum lifecycle_op { DISCARD, RESET, TRIM, SPLIT, PUNCH, LIFECYCLE_OPS };
union lifecycle_output {
    struct vm_disposition disposition;
    struct vm_region replacement;
    struct vm_regions parts;
};
static enum vm_error lifecycle_call(enum lifecycle_op op, struct fixture *x,
                                    struct vm_handle h, uint64_t off, uint64_t bytes,
                                    union lifecycle_output *out) {
    switch (op) {
    case DISCARD: return vm_discard(&x->space, h, off, bytes, out ? &out->disposition : NULL);
    case RESET: return vm_reset(&x->space, h, off, bytes, out ? &out->replacement : NULL);
    case TRIM: return vm_trim(&x->space, h, off, bytes, out ? &out->replacement : NULL);
    case SPLIT: return vm_split(&x->space, h, off, out ? &out->parts : NULL);
    case PUNCH: return vm_punch(&x->space, h, off, bytes, out ? &out->parts : NULL);
    default: abort();
    }
}
static void stale(struct fixture *x, struct vm_handle h) {
    struct vm_page_state out, poison;
    memset(&out, 0xe7, sizeof(out)); memcpy(&poison, &out, sizeof(out));
    ERROR(vm_query(&x->space, h, 0, &out), VM_STALE);
    assert(!memcmp(&out, &poison, sizeof(out)));
}
static void lifecycle_error(enum lifecycle_op op, struct vm_handle h, uint64_t off,
                             uint64_t bytes, enum vm_error expected, unsigned null_output) {
    union lifecycle_output out, poison;
    memset(&out, 0xa7, sizeof(out)); memcpy(&poison, &out, sizeof(out));
    snapshot(&f);
    ERROR(lifecycle_call(op, &f, h, off, bytes, null_output ? NULL : &out), expected);
    assert(!memcmp(&out, &poison, sizeof(out))); unchanged(&f);
    ++lifecycle_trials;
}
static void model_replace(struct fixture *x, struct vm_region old,
                           const struct vm_region *parts, unsigned count) {
    unsigned found = 0;
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i)
        if (x->regions[i].live && same_handle(x->regions[i].region.handle, old.handle)) {
            x->regions[i].live = 0; ++found;
        }
    assert(found == 1 && count <= 2);
    for (unsigned n = 0; n < count; ++n) {
        struct vm_region r = parts[n];
        assert(r.handle.space_id == old.handle.space_id && r.handle.generation);
        assert(r.handle.slot < VM_MAX_REGIONS && !same_handle(r.handle, old.handle));
        if (r.handle.slot == old.handle.slot) assert(r.handle.generation > old.handle.generation);
        assert(r.length && !(r.base & (VM_PAGE-1)) && !(r.length & (VM_PAGE-1)));
        assert(r.base >= old.base && r.base-old.base < old.length);
        assert(r.length <= old.length-(r.base-old.base));
        unsigned free_slot = VM_MAX_REGIONS;
        for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) {
            if (!x->regions[i].live) { free_slot = i; continue; }
            struct vm_region existing = x->regions[i].region;
            assert(r.base+r.length <= existing.base || existing.base+existing.length <= r.base);
            assert(!same_handle(r.handle, existing.handle));
        }
        assert(free_slot < VM_MAX_REGIONS);
        x->regions[free_slot] = (struct model_region){r, 1};
    }
    stale(x, old.handle);
}
static struct vm_regions lifecycle(struct fixture *x, enum lifecycle_op op,
                                   struct vm_region old, uint64_t off, uint64_t bytes) {
    struct vm_regions expected = {0}, actual = {0};
    struct vm_disposition disposition = {0};
    struct vm_frame_expectation { uint64_t va; struct frame_id id; unsigned live; } saved[MODEL_PAGES];
    memset(saved, 0, sizeof(saved));
    x->expected_retiring = 0;
    memset(x->retirement_mask, 0, sizeof(x->retirement_mask));
    x->free_before_retirement = x->pool.roles[FRAME_FREE];
    unsigned prior_flushes = x->flushes;
    uint64_t generation = x->pool.generation;
    if (op == DISCARD || op == RESET) {
        expected.count = 1; expected.regions[0] = old;
    } else if (op == TRIM) {
        expected.count = 1; expected.regions[0].base = old.base+off;
        expected.regions[0].length = bytes;
    } else if (op == SPLIT) {
        expected.count = 2;
        expected.regions[0].base = old.base; expected.regions[0].length = off;
        expected.regions[1].base = old.base+off; expected.regions[1].length = old.length-off;
    } else {
        if (off) {
            expected.regions[expected.count].base = old.base;
            expected.regions[expected.count++].length = off;
        }
        if (bytes < old.length-off) {
            expected.regions[expected.count].base = old.base+off+bytes;
            expected.regions[expected.count++].length = old.length-off-bytes;
        }
    }
    for (unsigned i = 0; i < MODEL_PAGES; ++i) if (x->pages[i].live) {
        struct model_page *p = &x->pages[i];
        if (p->va < old.base || p->va-old.base >= old.length) continue;
        uint64_t relative = p->va-old.base;
        unsigned selected = relative >= off && relative-off < bytes;
        unsigned removed = (op == RESET || op == PUNCH) ? selected :
                           op == TRIM ? !selected : op == DISCARD && selected && p->permission == VM_NONE;
        struct vm_page_state state;
        OK(vm_query(&x->space, old.handle, relative, &state)); assert(state.backed);
        if (removed) {
            x->retirement_mask[physical_index(x, state.frame.physical)] = 1;
            ++x->expected_retiring;
            if (op == DISCARD) ++disposition.released_pages;
        } else {
            saved[i] = (struct vm_frame_expectation){p->va, state.frame, 1};
            if (op == DISCARD && selected) ++disposition.zeroed_pages;
        }
    }
    union lifecycle_output out; memset(&out, 0xa7, sizeof(out));
    uint64_t operation_start = x->space.operation_visits;
    OK(lifecycle_call(op, x, old.handle, off, bytes, &out));
    lifecycle_visits = x->space.operation_visits-operation_start;
    /* No lifecycle operation is allowed to allocate or issue a frame epoch. */
    assert(x->pool.generation == generation);
    if (x->expected_retiring) assert(x->flushes > prior_flushes);
    if (op == SPLIT) assert(x->flushes == prior_flushes);
    retirement_complete(x);
    if (op == DISCARD) {
        assert(out.disposition.zeroed_pages == disposition.zeroed_pages);
        assert(out.disposition.released_pages == disposition.released_pages);
        actual = expected;
    } else if (op == RESET || op == TRIM) {
        actual.count = 1; actual.regions[0] = out.replacement;
    } else actual = out.parts;
    assert(actual.count == expected.count);
    for (unsigned i = 0; i < actual.count; ++i) {
        assert(actual.regions[i].base == expected.regions[i].base);
        assert(actual.regions[i].length == expected.regions[i].length);
    }
    for (unsigned i = 0; i < MODEL_PAGES; ++i) if (x->pages[i].live) {
        struct model_page *p = &x->pages[i];
        if (p->va < old.base || p->va-old.base >= old.length) continue;
        if (!saved[i].live) p->live = 0;
        else if (op == DISCARD && p->va-old.base >= off && p->va-old.base-off < bytes) p->pattern = 0;
    }
    if (op != DISCARD) model_replace(x, old, actual.regions, actual.count);
    for (unsigned i = 0; i < MODEL_PAGES; ++i) if (saved[i].live) {
        unsigned found = 0;
        for (unsigned j = 0; j < actual.count; ++j) {
            struct vm_region r = actual.regions[j];
            if (saved[i].va < r.base || saved[i].va-r.base >= r.length) continue;
            struct vm_page_state state;
            OK(vm_query(&x->space, r.handle, saved[i].va-r.base, &state));
            assert(state.backed && state.frame.physical == saved[i].id.physical);
            assert(state.frame.generation == saved[i].id.generation); ++found;
        }
        assert(found == 1);
    }
    audit(x); ++lifecycle_trials;
    return actual;
}
static void release_all(struct fixture *x) {
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i)
        if (x->regions[i].live) release(x, x->regions[i].region);
    audit(x); assert(x->pool.roles[FRAME_FREE] == x->pool.selection.managed_count);
}
static void test_lifecycle_arguments(void) {
    start(&f, NORMAL_FRAMES); start(&other, 16);
    struct vm_region r = reserve_at(&f, 8*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    struct vm_region foreign = reserve_at(&other, 8*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    commit(&f, r, 0, 4, VM_READ_WRITE); paint(&f, r, 0, 0xe1);
    protect(&f, r, VM_PAGE, VM_PAGE, VM_NONE);
    for (enum lifecycle_op op = DISCARD; op < LIFECYCLE_OPS; ++op) {
        lifecycle_error(op, foreign.handle, VM_PAGE, VM_PAGE, VM_FOREIGN, 0);
        struct vm_handle forged = r.handle; ++forged.generation;
        lifecycle_error(op, forged, VM_PAGE, VM_PAGE, VM_STALE, 0);
        forged = r.handle; forged.generation = 0;
        lifecycle_error(op, forged, VM_PAGE, VM_PAGE, VM_STALE, 0);
        forged = r.handle; forged.slot = VM_MAX_REGIONS;
        lifecycle_error(op, forged, VM_PAGE, VM_PAGE, VM_STALE, 0);
        lifecycle_error(op, r.handle, VM_PAGE, VM_PAGE, VM_ARGUMENT, 1);
        lifecycle_error(op, r.handle, 1, VM_PAGE, VM_ALIGNMENT, 0);
        lifecycle_error(op, r.handle, r.length, VM_PAGE, VM_RANGE, 0);
        lifecycle_error(op, r.handle, UINT64_MAX-VM_PAGE+1, VM_PAGE, VM_RANGE, 0);
        if (op != SPLIT) {
            lifecycle_error(op, r.handle, 0, 0, VM_ARGUMENT, 0);
            lifecycle_error(op, r.handle, 0, VM_PAGE+1, VM_ALIGNMENT, 0);
            lifecycle_error(op, r.handle, VM_PAGE, UINT64_MAX-VM_PAGE+1, VM_RANGE, 0);
            lifecycle_error(op, r.handle, 7*VM_PAGE, 2*VM_PAGE, VM_RANGE, 0);
        }
        union lifecycle_output out, poison; memset(&out, 0x9a, sizeof(out)); memcpy(&poison, &out, sizeof(out));
        snapshot(&f); f.context = 0;
        ERROR(lifecycle_call(op, &f, r.handle, VM_PAGE, VM_PAGE, &out), VM_STATE);
        f.context = 1; assert(!memcmp(&out, &poison, sizeof(out))); unchanged(&f);
        snapshot(&f); f.space.busy = 1;
        ERROR(lifecycle_call(op, &f, r.handle, VM_PAGE, VM_PAGE, &out), VM_STATE);
        f.space.busy = 0; assert(!memcmp(&out, &poison, sizeof(out))); unchanged(&f);
        lifecycle_trials += 2;
    }
    /* Split at either end cannot produce the two required nonempty halves. */
    lifecycle_error(SPLIT, r.handle, 0, 0, VM_ARGUMENT, 0);
    struct vm_region old = r;
    r = lifecycle(&f, RESET, r, VM_PAGE, VM_PAGE).regions[0];
    for (enum lifecycle_op op = DISCARD; op < LIFECYCLE_OPS; ++op)
        lifecycle_error(op, old.handle, VM_PAGE, VM_PAGE, VM_STALE, 0);
    release_all(&f); release_all(&other);
    puts("VM_HOST_LIFECYCLE_ARGUMENTS_ATOMIC_PASS");
}
static void test_lifecycle_bytes_and_neighbors(void) {
    start(&f, NORMAL_FRAMES);
    struct vm_region a = reserve_at(&f, VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START);
    struct vm_region r = reserve_at(&f, 12*VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START+VM_PAGE);
    struct vm_region b = reserve_at(&f, VM_PAGE, VM_PAGE, VM_EXACT, r.base+r.length);
    commit(&f, a, 0, 1, VM_READ_WRITE); paint(&f, a, 0, 0x63);
    commit(&f, b, 0, 1, VM_READ_WRITE); paint(&f, b, 0, 0x91);
    protect(&f, b, 0, VM_PAGE, VM_NONE);
    commit(&f, r, VM_PAGE, 5, VM_READ_WRITE);
    for (unsigned i = 1; i <= 5; ++i) paint(&f, r, (uint64_t)i*VM_PAGE, (unsigned char)(0x20+i));
    protect(&f, r, 2*VM_PAGE, VM_PAGE, VM_READ);
    protect(&f, r, 3*VM_PAGE, 2*VM_PAGE, VM_NONE);
    lifecycle(&f, DISCARD, r, 0, 5*VM_PAGE);
    lifecycle(&f, DISCARD, r, 0, 5*VM_PAGE); /* Repeated discard remains deterministic. */
    assert(page_find(&f, r.base) < 0 && page_find(&f, r.base+3*VM_PAGE) < 0);
    commit(&f, r, 3*VM_PAGE, 2, VM_READ_WRITE); audit(&f); /* Removed NONE starts zero. */
    r = lifecycle(&f, RESET, r, 2*VM_PAGE, 2*VM_PAGE).regions[0];
    commit(&f, r, 2*VM_PAGE, 2, VM_READ_WRITE); audit(&f);
    struct vm_regions halves = lifecycle(&f, SPLIT, r, 6*VM_PAGE, 0);
    struct vm_region left = halves.regions[0], right = halves.regions[1];
    /* Full-interval trim still replaces the token without changing any byte. */
    left = lifecycle(&f, TRIM, left, 0, left.length).regions[0];
    right = lifecycle(&f, TRIM, right, VM_PAGE, 4*VM_PAGE).regions[0];
    struct vm_region hole = reserve_at(&f, VM_PAGE, VM_PAGE, VM_EXACT, halves.regions[1].base);
    commit(&f, hole, 0, 1, VM_READ_WRITE); paint(&f, hole, 0, 0xa9);
    struct vm_regions pieces = lifecycle(&f, PUNCH, left, 2*VM_PAGE, 2*VM_PAGE);
    assert(pieces.count == 2);
    struct vm_region reclaimed = reserve_at(&f, 2*VM_PAGE, VM_PAGE, VM_EXACT, left.base+2*VM_PAGE);
    commit(&f, reclaimed, 0, 2, VM_READ_WRITE); audit(&f);
    pieces.regions[0] = lifecycle(&f, PUNCH, pieces.regions[0], 0, VM_PAGE).regions[0];
    pieces.regions[1] = lifecycle(&f, PUNCH, pieces.regions[1], VM_PAGE, VM_PAGE).regions[0];
    assert(lifecycle(&f, PUNCH, pieces.regions[0], 0, VM_PAGE).count == 0);
    /* Leave only neighboring resident-NONE backing on its original shared path. */
    release(&f, a); release(&f, right); release(&f, hole); release(&f, reclaimed);
    release(&f, pieces.regions[1]); audit(&f);
    assert(f.pool.roles[FRAME_DATA] == 1 && f.pool.roles[FRAME_PT] == 1);
    protect(&f, b, 0, VM_PAGE, VM_READ_WRITE); audit(&f);
    release_all(&f);
    puts("VM_HOST_LIFECYCLE_BYTES_INTERVALS_SHARED_PASS");
}
static struct vm_region lifecycle_fixture(void) {
    start(&f, NORMAL_FRAMES);
    struct vm_region r = reserve_at(&f, 8*VM_PAGE, VM_PAGE, VM_EXACT, VM_ARENA_START);
    struct vm_region n = reserve_at(&f, VM_PAGE, VM_PAGE, VM_EXACT, r.base+r.length);
    commit(&f, r, 0, 8, VM_READ_WRITE); commit(&f, n, 0, 1, VM_READ_WRITE);
    for (unsigned i = 0; i < 8; ++i) paint(&f, r, (uint64_t)i*VM_PAGE, (unsigned char)(0x61+i));
    paint(&f, n, 0, 0xeb); protect(&f, r, VM_PAGE, VM_PAGE, VM_READ);
    protect(&f, r, 3*VM_PAGE, VM_PAGE, VM_NONE); protect(&f, n, 0, VM_PAGE, VM_NONE);
    return r;
}
static void test_lifecycle_metadata_failures(void) {
    const struct { enum lifecycle_op op; uint64_t off, bytes; unsigned outputs; } cases[] = {
        {RESET, 2*VM_PAGE, 2*VM_PAGE, 1}, {TRIM, 2*VM_PAGE, 4*VM_PAGE, 1},
        {SPLIT, 4*VM_PAGE, 0, 2}, {PUNCH, 2*VM_PAGE, 2*VM_PAGE, 2},
        {PUNCH, 0, 2*VM_PAGE, 1}, {PUNCH, 6*VM_PAGE, 2*VM_PAGE, 1},
        {PUNCH, 0, 8*VM_PAGE, 0}, {DISCARD, 0, 8*VM_PAGE, 0}
    };
    for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); ++c) {
        for (unsigned nth = 1; nth <= cases[c].outputs; ++nth) {
            struct vm_region r = lifecycle_fixture();
            uint64_t epoch = f.pool.generation;
            OK(vm_fail_metadata_after(&f.space, nth));
            lifecycle_error(cases[c].op, r.handle, cases[c].off, cases[c].bytes, VM_INJECTED, 0);
            assert(f.space.metadata_attempt == nth && f.pool.generation == epoch);
            ++metadata_trials;
            OK(vm_fail_metadata_after(&f.space, 0));
            lifecycle(&f, cases[c].op, r, cases[c].off, cases[c].bytes);
            release_all(&f);
        }
        /* One beyond the actual output count must not inject. In particular,
         * discard and a complete punch select no replacement metadata at all. */
        struct vm_region r = lifecycle_fixture();
        OK(vm_fail_metadata_after(&f.space, cases[c].outputs+1));
        lifecycle(&f, cases[c].op, r, cases[c].off, cases[c].bytes);
        assert(f.space.metadata_attempt == cases[c].outputs);
        OK(vm_fail_metadata_after(&f.space, 0)); release_all(&f);
    }
    /* Injection is cumulative since configuration, fires once at the selected
     * output, and its diagnostic counter cannot wrap into a second injection. */
    struct vm_region r = lifecycle_fixture();
    OK(vm_fail_metadata_after(&f.space, 2));
    r = lifecycle(&f, TRIM, r, 0, r.length).regions[0];
    assert(f.space.metadata_attempt == 1);
    lifecycle_error(RESET, r.handle, 0, VM_PAGE, VM_INJECTED, 0); ++metadata_trials;
    assert(f.space.metadata_attempt == 2);
    r = lifecycle(&f, RESET, r, 0, VM_PAGE).regions[0];
    assert(f.space.metadata_attempt == 3);
    lifecycle(&f, SPLIT, r, 4*VM_PAGE, 0); assert(f.space.metadata_attempt == 5);
    OK(vm_fail_metadata_after(&f.space, 0)); assert(!f.space.metadata_attempt); release_all(&f);
    r = lifecycle_fixture();
    OK(vm_fail_metadata_after(&f.space, UINT32_MAX));
    f.space.metadata_attempt = UINT32_MAX-2; /* Diagnostic-only exhaustion fixture. */
    r = lifecycle(&f, TRIM, r, 0, r.length).regions[0];
    assert(f.space.metadata_attempt == UINT32_MAX-1);
    lifecycle_error(RESET, r.handle, 0, VM_PAGE, VM_INJECTED, 0); ++metadata_trials;
    assert(f.space.metadata_attempt == UINT32_MAX);
    r = lifecycle(&f, RESET, r, 0, VM_PAGE).regions[0];
    assert(f.space.metadata_attempt == UINT32_MAX);
    lifecycle(&f, SPLIT, r, 4*VM_PAGE, 0); assert(f.space.metadata_attempt == UINT32_MAX);
    OK(vm_fail_metadata_after(&f.space, 0)); release_all(&f);
    printf("VM_HOST_LIFECYCLE_METADATA_ATOMIC_PASS trials=%u\n", metadata_trials);
}
static void test_lifecycle_slots_and_epochs(void) {
    for (unsigned terminal = 0; terminal < 2; ++terminal) {
        start(&f, NORMAL_FRAMES);
        if (terminal) f.space.regions[0].generation = UINT64_MAX-1;
        struct vm_region r = reserve_at(&f, 8*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
        struct vm_region all[VM_MAX_REGIONS-1];
        for (unsigned i = 0; i < VM_MAX_REGIONS-1; ++i)
            all[i] = reserve_at(&f, VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
        commit(&f, r, 0, 8, VM_READ_WRITE); paint(&f, r, 0, 0xd1);
        commit(&f, all[0], 0, 1, VM_READ); paint(&f, all[0], 0, 0x54);
        commit(&f, all[1], 0, 1, VM_READ_WRITE); paint(&f, all[1], 0, 0x8c);
        protect(&f, all[1], 0, VM_PAGE, VM_NONE);
        protect(&f, r, 2*VM_PAGE, VM_PAGE, VM_NONE);
        lifecycle_error(SPLIT, r.handle, 4*VM_PAGE, 0, VM_LIMIT, 0);
        lifecycle_error(PUNCH, r.handle, 2*VM_PAGE, 2*VM_PAGE, VM_LIMIT, 0);
        if (terminal) {
            lifecycle_error(RESET, r.handle, 0, VM_PAGE, VM_LIMIT, 0);
            lifecycle_error(TRIM, r.handle, VM_PAGE, 6*VM_PAGE, VM_LIMIT, 0);
            lifecycle_error(PUNCH, r.handle, 0, VM_PAGE, VM_LIMIT, 0);
            lifecycle(&f, DISCARD, r, 0, r.length);
            assert(lifecycle(&f, PUNCH, r, 0, r.length).count == 0);
        } else {
            r = lifecycle(&f, RESET, r, VM_PAGE, VM_PAGE).regions[0];
            r = lifecycle(&f, TRIM, r, 0, r.length).regions[0];
            r = lifecycle(&f, PUNCH, r, 0, VM_PAGE).regions[0];
            r = lifecycle(&f, PUNCH, r, r.length-VM_PAGE, VM_PAGE).regions[0];
            release(&f, all[17]);
            struct vm_regions two = lifecycle(&f, SPLIT, r, 3*VM_PAGE, 0);
            lifecycle_error(PUNCH, two.regions[0].handle, VM_PAGE, VM_PAGE, VM_LIMIT, 0);
            release(&f, all[18]);
            lifecycle(&f, PUNCH, two.regions[0], VM_PAGE, VM_PAGE);
        }
        release_all(&f);
    }
    /* Terminal input authority is genuinely issued by reserve, never created by
     * altering a live generation. Every spare output epoch is preflighted. */
    for (enum lifecycle_op op = RESET; op <= PUNCH; ++op) {
        start(&f, NORMAL_FRAMES);
        for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) f.space.regions[i].generation = UINT64_MAX;
        f.space.regions[7].generation = UINT64_MAX-1;
        struct vm_region r = reserve_at(&f, 8*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
        assert(r.handle.slot == 7 && r.handle.generation == UINT64_MAX);
        commit(&f, r, 0, 8, VM_READ_WRITE); paint(&f, r, 7*VM_PAGE, 0xc4);
        uint64_t off = op == SPLIT ? 4*VM_PAGE : 2*VM_PAGE;
        uint64_t bytes = 2*VM_PAGE;
        lifecycle_error(op, r.handle, off, bytes, VM_LIMIT, 0);
        f.space.regions[9].generation = UINT64_MAX-1;
        if (op == SPLIT || op == PUNCH) {
            lifecycle_error(op, r.handle, off, bytes, VM_LIMIT, 0);
            f.space.regions[13].generation = UINT64_MAX-1;
        }
        struct vm_regions out = lifecycle(&f, op, r, off, bytes);
        for (unsigned i = 0; i < out.count; ++i) {
            assert(out.regions[i].handle.slot != 7 && out.regions[i].handle.generation == UINT64_MAX);
            lifecycle_error(RESET, out.regions[i].handle, 0, VM_PAGE, VM_LIMIT, 0);
        }
        assert(!f.space.regions[7].live && f.space.regions[7].generation == UINT64_MAX);
        release_all(&f);
        expect_reserve_error(VM_PAGE, VM_PAGE, VM_ANYWHERE, 0, VM_LIMIT);
    }
    start(&f, NORMAL_FRAMES);
    for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) f.space.regions[i].generation = UINT64_MAX;
    f.space.regions[7].generation = UINT64_MAX-2; f.space.regions[9].generation = UINT64_MAX-1;
    struct vm_region r = reserve_at(&f, 2*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    assert(r.handle.generation == UINT64_MAX-1);
    r = lifecycle(&f, RESET, r, 0, VM_PAGE).regions[0];
    assert(r.handle.slot == 7 && r.handle.generation == UINT64_MAX);
    r = lifecycle(&f, RESET, r, 0, VM_PAGE).regions[0];
    assert(r.handle.slot == 9 && r.handle.generation == UINT64_MAX);
    lifecycle_error(TRIM, r.handle, 0, r.length, VM_LIMIT, 0);
    lifecycle(&f, PUNCH, r, 0, r.length); release_all(&f);
    puts("VM_HOST_LIFECYCLE_SLOTS_NONWRAPPING_PASS");
}
static void test_lifecycle_giant_and_no_frames(void) {
    start(&f, NORMAL_FRAMES);
    struct vm_region r = reserve_at(&f, GIANT, BOUNDARY_2M, VM_EXACT, VM_ARENA_START);
    struct vm_counts empty_before, empty_after;
    OK(vm_stats(&f.space, &empty_before));
    uint64_t empty_epoch = f.pool.generation;
    unsigned empty_flushes = f.flushes;
    r = lifecycle(&f, TRIM, r, 0, UINT64_C(0x15800000000)).regions[0];
    OK(vm_stats(&f.space, &empty_after));
    assert(!empty_after.data && !empty_after.pt && !empty_after.pd && !empty_after.pdpt);
    assert(empty_after.free_frames == empty_before.free_frames && f.pool.generation == empty_epoch);
    assert(f.flushes == empty_flushes && lifecycle_visits < UINT64_C(1000000));
    release_all(&f);
    start(&f, NORMAL_FRAMES);
    r = reserve_at(&f, GIANT, BOUNDARY_2M, VM_EXACT, VM_ARENA_START);
    commit(&f, r, UINT64_C(0x1000000000), 64, VM_READ_WRITE);
    paint(&f, r, UINT64_C(0x1000000000), 0x81);
    uint64_t pool_epoch = f.pool.generation;
    r = lifecycle(&f, TRIM, r, 0, UINT64_C(0x15800000000)).regions[0];
    assert(f.pool.generation == pool_epoch);
    assert(lifecycle_visits < UINT64_C(1000000));
    commit(&f, r, UINT64_C(0xac00000000), 1, VM_READ_WRITE);
    commit(&f, r, UINT64_C(0x157fffff000), 1, VM_READ);
    paint(&f, r, UINT64_C(0xac00000000), 0x93);
    paint(&f, r, UINT64_C(0x157fffff000), 0xbe); audit(&f);
    struct vm_counts counts; OK(vm_stats(&f.space, &counts));
    assert(counts.data == 66 && counts.pt+counts.pd+counts.pdpt == 9);
    protect(&f, r, UINT64_C(0x1000000000), 64*VM_PAGE, VM_NONE);
    lifecycle(&f, DISCARD, r, UINT64_C(0x1000000000), 64*VM_PAGE);
    OK(vm_stats(&f.space, &counts)); assert(counts.data == 2);
    commit(&f, r, UINT64_C(0x1000000000), 64, VM_READ_WRITE); audit(&f);
    /* Reset an actual sparse 4 GiB window, with data near each side and its
     * midpoint. Adjacent outside pages remain part of the renewed interval. */
    const uint64_t near = UINT64_C(0x1000000000), four_gib = UINT64_C(0x100000000);
    commit(&f, r, near+four_gib/2, 1, VM_READ);
    commit(&f, r, near+four_gib-VM_PAGE, 1, VM_READ_WRITE);
    paint(&f, r, near, 0x47); paint(&f, r, near+four_gib/2, 0x59);
    paint(&f, r, near+four_gib-VM_PAGE, 0x6b);
    protect(&f, r, near+four_gib-VM_PAGE, VM_PAGE, VM_NONE);
    commit(&f, r, near-VM_PAGE, 1, VM_READ); paint(&f, r, near-VM_PAGE, 0x7d);
    commit(&f, r, near+four_gib, 1, VM_READ_WRITE); paint(&f, r, near+four_gib, 0x8f);
    protect(&f, r, near+four_gib, VM_PAGE, VM_NONE); audit(&f);
    r = lifecycle(&f, RESET, r, near, four_gib).regions[0];
    assert(lifecycle_visits < UINT64_C(1000000));
    OK(vm_stats(&f.space, &counts)); assert(counts.data == 4);
    check_query(&f, r, near); check_query(&f, r, near+four_gib/2);
    check_query(&f, r, near+four_gib-VM_PAGE);
    commit(&f, r, near, 64, VM_READ_WRITE);
    commit(&f, r, near+four_gib/2, 1, VM_READ_WRITE);
    commit(&f, r, near+four_gib-VM_PAGE, 1, VM_READ_WRITE); audit(&f);
    /* Kept base need only retain page alignment, not the original 2 MiB hint. */
    r = lifecycle(&f, TRIM, r, VM_PAGE, r.length-2*VM_PAGE).regions[0];
    assert(r.base == VM_ARENA_START+VM_PAGE && (r.base & (BOUNDARY_2M-1)) == VM_PAGE);
    assert(lifecycle_visits < UINT64_C(1000000));
    lifecycle(&f, DISCARD, r, 0, r.length);
    assert(lifecycle_visits < UINT64_C(1000000));
    struct vm_regions halves = lifecycle(&f, SPLIT, r, BOUNDARY_512G, 0);
    lifecycle(&f, PUNCH, halves.regions[1], VM_PAGE, halves.regions[1].length-2*VM_PAGE);
    assert(lifecycle_visits < UINT64_C(1000000)); release_all(&f);
    start(&f, 4);
    r = reserve_at(&f, 4*VM_PAGE, VM_PAGE, VM_ANYWHERE, 0);
    commit(&f, r, VM_PAGE, 1, VM_READ_WRITE); paint(&f, r, VM_PAGE, 0xca);
    assert(!f.pool.roles[FRAME_FREE]);
    r = lifecycle(&f, TRIM, r, 0, 3*VM_PAGE).regions[0];
    halves = lifecycle(&f, SPLIT, r, VM_PAGE, 0);
    lifecycle(&f, DISCARD, halves.regions[1], 0, halves.regions[1].length);
    r = lifecycle(&f, RESET, halves.regions[1], 0, VM_PAGE).regions[0];
    assert(f.pool.roles[FRAME_FREE] == 4);
    commit(&f, r, 0, 1, VM_READ_WRITE); audit(&f);
    lifecycle(&f, PUNCH, r, 0, r.length); release_all(&f);
    puts("VM_HOST_LIFECYCLE_GIANT_REFERENCE_BOUNDS_NOFRAMES_PASS");
}
static void test_lifecycle_fragmented_sequences(void) {
    const uint32_t seeds[] = {UINT32_C(0xbca98731), UINT32_C(0x671ca543), UINT32_C(0x2b49e8d1)};
    unsigned operation_counts[LIFECYCLE_OPS] = {0}, steps = 0;
    for (unsigned seed = 0; seed < sizeof(seeds)/sizeof(seeds[0]); ++seed) {
        start(&f, 768); random_state = seeds[seed];
        for (unsigned step = 0; step < 320; ++step) {
            unsigned live = 0;
            for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) live += f.regions[i].live;
            if (!live || (live < 32 && random_next()%5 == 0)) {
                unsigned block = random_next()%64, overlap = 0;
                uint64_t base = VM_ARENA_START+(uint64_t)(block/8)*BOUNDARY_512G+
                                (uint64_t)(block%8)*BOUNDARY_2M;
                for (unsigned i = 0; i < VM_MAX_REGIONS; ++i) if (f.regions[i].live) {
                    struct vm_region r = f.regions[i].region;
                    overlap |= r.base < base+12*VM_PAGE && base < r.base+r.length;
                }
                if (!overlap) reserve_at(&f, 12*VM_PAGE, VM_PAGE, VM_EXACT, base);
            } else {
                unsigned chosen = random_next()%live, index = 0;
                for (; index < VM_MAX_REGIONS; ++index)
                    if (f.regions[index].live && !chosen--) break;
                assert(index < VM_MAX_REGIONS);
                struct vm_region r = f.regions[index].region;
                unsigned pages = (unsigned)(r.length/VM_PAGE), first = random_next()%pages;
                unsigned number = 1+random_next()%(pages-first), action = random_next()%10;
                uint64_t off = (uint64_t)first*VM_PAGE, bytes = (uint64_t)number*VM_PAGE;
                if (action == 0) {
                    commit(&f, r, off, number, random_next()%2 ? VM_READ : VM_READ_WRITE);
                    paint(&f, r, off, (unsigned char)(1+step%254));
                } else if (action == 1) protect(&f, r, off, bytes, VM_NONE);
                else if (action >= 2 && action <= 6) {
                    enum lifecycle_op op = (enum lifecycle_op)(action-2);
                    if (op == SPLIT) {
                        if (pages == 1) op = DISCARD;
                        else off = (uint64_t)(1+random_next()%(pages-1))*VM_PAGE;
                    }
                    lifecycle(&f, op, r, off, bytes); ++operation_counts[op];
                } else if (action == 7) release(&f, r);
                else if (action == 8) decommit(&f, r, off, bytes);
                else {
                    unsigned missing = 0;
                    for (unsigned p = 0; p < number; ++p)
                        missing |= page_find(&f, r.base+off+(uint64_t)p*VM_PAGE) < 0;
                    if (missing) {
                        snapshot(&f);
                        ERROR(vm_protect(&f.space, r.handle, off, bytes, VM_READ), VM_UNBACKED);
                        unchanged(&f);
                    } else protect(&f, r, off, bytes, VM_READ);
                }
            }
            audit(&f); ++steps;
        }
        release_all(&f);
    }
    for (unsigned op = 0; op < LIFECYCLE_OPS; ++op) assert(operation_counts[op] >= 20);
    printf("VM_HOST_LIFECYCLE_FRAGMENTED_ORACLE_PASS seeds=0xbca98731,0x671ca543,0x2b49e8d1 operations=%u discard=%u reset=%u trim=%u split=%u punch=%u\n",
           steps, operation_counts[DISCARD], operation_counts[RESET], operation_counts[TRIM],
           operation_counts[SPLIT], operation_counts[PUNCH]);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    test_arguments_ownership_and_slots();
    test_giant_preservation_and_shared_paths();
    test_boundaries_and_limits();
    test_allocation_failures();
    test_fragmented_model();
    test_corruption_detection();
    test_nonwrapping_and_context();
    printf("VM_HOST_CORE_PASS checkpoints=%u allocation_failure_trials=%u byte_backed_host_only=1\n",
           checkpoints, injection_trials);
    assert(checkpoints == 1366 && injection_trials == 299);
    unsigned legacy_checkpoints = checkpoints;
    test_lifecycle_arguments();
    test_lifecycle_bytes_and_neighbors();
    test_lifecycle_metadata_failures();
    test_lifecycle_slots_and_epochs();
    test_lifecycle_giant_and_no_frames();
    test_lifecycle_fragmented_sequences();
    printf("VM_HOST_LIFECYCLE_CORE_PASS checkpoints=%u lifecycle_trials=%u metadata_failure_trials=%u byte_backed_host_only=1\n",
           checkpoints-legacy_checkpoints, lifecycle_trials, metadata_trials);
    return 0;
}

#include "frame_pool.h"

static void clear_pool(struct frame_pool *p) {
    unsigned char *b=(unsigned char *)p;
    for (size_t i=0;i<sizeof(*p);++i) b[i]=0;
}
static int valid_alias(struct frame_pool *p, uint64_t physical) {
    return (p->platform.read_leaf(p->platform.opaque,physical)&~UINT64_C(0x60)) ==
        (physical|FRAME_POOL_NX|3);
}
static enum frame_error enter(struct frame_pool *p) {
    if (!p || p->ready!=1 || p->busy) return FRAME_BAD_STATE;
    p->busy=1;
    if (!p->platform.context_ok(p->platform.opaque)) { p->busy=0; return FRAME_BAD_CONTEXT; }
    return FRAME_OK;
}
static enum frame_error leave(struct frame_pool *p,enum frame_error e) { p->busy=0;return e; }
static enum frame_error audit(struct frame_pool *p) {
    uint32_t counts[FRAME_ROLE_COUNT]={0};
    if (!p->selection.managed_count || p->selection.managed_count>BOOT_MEMORY_MAX_FRAMES ||
        p->selection.eligible_count<p->selection.managed_count) return FRAME_CORRUPT;
    for (uint32_t i=0;i<p->selection.managed_count;++i) {
        uint64_t a=p->selection.frames[i];
        struct frame_record *r=&p->records[i];
        if (a<0x100000 || a>=BOOTINFO_CEILING || (a&4095) ||
            (i && a<=p->selection.frames[i-1]) || !valid_alias(p,a) ||
            (unsigned)r->role>=FRAME_ROLE_COUNT || r->generation>p->generation ||
            (r->role==FRAME_FREE ? r->owner!=0 : (!r->owner || !r->generation))) return FRAME_CORRUPT;
        ++counts[r->role];
    }
    for (unsigned i=0;i<FRAME_ROLE_COUNT;++i) if (counts[i]!=p->roles[i]) return FRAME_CORRUPT;
    return FRAME_OK;
}
static enum frame_error checked_enter(struct frame_pool *p) {
    enum frame_error e=enter(p);
    if (e) return e;
    e=audit(p);
    return e ? leave(p,e) : FRAME_OK;
}
enum frame_error frame_pool_init(struct frame_pool *p,const void *bytes,size_t available,
    const struct boot_memory_request *request,const struct frame_platform *platform,const char **boot_error) {
    if (!p || !platform || !platform->context_ok || !platform->read_leaf || !platform->write_leaf ||
        !platform->flush || !platform->alias) return FRAME_BAD_ARGUMENT;
    if (p->ready || p->busy) return FRAME_BAD_STATE;
    p->busy=1;
    if (!platform->context_ok(platform->opaque)) { p->busy=0;return FRAME_BAD_CONTEXT; }
    const char *error=boot_memory_select(bytes,available,request,&p->selection);
    if (error) { clear_pool(p);if (boot_error) *boot_error=error;return FRAME_BOOT_MEMORY; }
    p->platform=*platform;
    uint32_t count=p->selection.managed_count;
    /* All possible conflicts checked before even the first leaf write. Nonzero
     * nonpresent leaves are conflicts too: no software-owned PTE is overwritten. */
    for (uint32_t i=0;i<count;++i)
        if (p->platform.read_leaf(p->platform.opaque,p->selection.frames[i])) {
            clear_pool(p);return FRAME_ALIAS_CONFLICT;
        }
    for (uint32_t i=0;i<count;++i)
        p->platform.write_leaf(p->platform.opaque,p->selection.frames[i],p->selection.frames[i]|FRAME_POOL_NX|3);
    p->platform.flush(p->platform.opaque);
    p->roles[FRAME_FREE]=count;
    p->ready=1;
    enum frame_error e=audit(p);
    if (e) {
        for (uint32_t i=0;i<count;++i) p->platform.write_leaf(p->platform.opaque,p->selection.frames[i],0);
        p->platform.flush(p->platform.opaque);
        clear_pool(p);return e;
    }
    p->busy=0;
    return FRAME_OK;
}
enum frame_error frame_pool_audit(struct frame_pool *p) {
    enum frame_error e=checked_enter(p);return e ? e : leave(p,FRAME_OK);
}
enum frame_error frame_pool_stats(struct frame_pool *p,struct frame_stats *out) {
    if (!out) return FRAME_BAD_ARGUMENT;
    enum frame_error e=checked_enter(p);if (e) return e;
    struct frame_stats s={0};
    s.firmware_usable_bytes=p->selection.firmware_usable_bytes;
    s.eligible=p->selection.eligible_count;s.managed=p->selection.managed_count;
    s.permanent_aliases=s.managed;
    /* PML4 + PDPT + PD + 32 PTs are kernel-image allocations, never pool frames. */
    s.borrowed_table_frames=35;
    for (unsigned i=0;i<FRAME_ROLE_COUNT;++i) s.roles[i]=p->roles[i];
    *out=s;return leave(p,FRAME_OK);
}
static enum frame_error lookup(struct frame_pool *p,struct frame_id id,uint64_t owner,uint32_t *slot) {
    if (!owner) return FRAME_BAD_ARGUMENT;
    for (uint32_t i=0;i<p->selection.managed_count;++i) if (p->selection.frames[i]==id.physical) {
        struct frame_record *r=&p->records[i];
        if (r->generation!=id.generation || r->role==FRAME_FREE) return FRAME_STALE;
        if (r->owner!=owner) return FRAME_WRONG_OWNER;
        *slot=i;return FRAME_OK;
    }
    return FRAME_FOREIGN;
}
static void role(struct frame_pool *p,uint32_t i,enum frame_role next) {
    --p->roles[p->records[i].role];++p->roles[next];p->records[i].role=next;
    if (next==FRAME_FREE) p->records[i].owner=0;
}
enum frame_error frame_pool_allocate(struct frame_pool *p,enum frame_role staged,uint64_t owner,struct frame_id *out) {
    if (!out || !owner || (staged!=FRAME_STAGED_DATA && staged!=FRAME_STAGED_TABLE)) return FRAME_BAD_ARGUMENT;
    enum frame_error e=checked_enter(p);if (e) return e;
    if (p->fail_nth && ++p->allocation_attempt==p->fail_nth) return leave(p,FRAME_INJECTED);
    if (p->generation==UINT64_MAX) return leave(p,FRAME_ID_EXHAUSTED);
    for (uint32_t i=0;i<p->selection.managed_count;++i) if (p->records[i].role==FRAME_FREE) {
        uint64_t physical=p->selection.frames[i];
        volatile unsigned char *address=p->platform.alias(p->platform.opaque,physical);
        if (!address) return leave(p,FRAME_CORRUPT);
#if FRAME_TEST_INJECT != 1
        for (uint32_t j=0;j<4096;++j) address[j]=0;
#endif
        __asm__ volatile("":::"memory");
        p->records[i].owner=owner;p->records[i].generation=++p->generation;
        role(p,i,staged);
        *out=(struct frame_id){physical,p->generation};return leave(p,FRAME_OK);
    }
    return leave(p,FRAME_EXHAUSTED);
}
enum frame_error frame_pool_promote(struct frame_pool *p,struct frame_id id,uint64_t owner,enum frame_role final) {
    if (final!=FRAME_DATA && final!=FRAME_PT && final!=FRAME_PD && final!=FRAME_PDPT) return FRAME_BAD_ARGUMENT;
    enum frame_error e=checked_enter(p);if (e) return e;
    uint32_t i;e=lookup(p,id,owner,&i);
    if (!e && p->records[i].role!=(final==FRAME_DATA?FRAME_STAGED_DATA:FRAME_STAGED_TABLE)) e=FRAME_WRONG_ROLE;
    if (!e) role(p,i,final);
    return leave(p,e);
}
enum frame_error frame_pool_cancel(struct frame_pool *p,struct frame_id id,uint64_t owner) {
    enum frame_error e=checked_enter(p);if (e) return e;
    uint32_t i;e=lookup(p,id,owner,&i);
    if (!e && p->records[i].role!=FRAME_STAGED_DATA && p->records[i].role!=FRAME_STAGED_TABLE) e=FRAME_WRONG_ROLE;
    if (!e) role(p,i,FRAME_FREE);
    return leave(p,e);
}
enum frame_error frame_pool_retire(struct frame_pool *p,struct frame_id id,uint64_t owner,enum frame_role expected) {
    if (expected!=FRAME_DATA && expected!=FRAME_PT && expected!=FRAME_PD && expected!=FRAME_PDPT) return FRAME_BAD_ARGUMENT;
    enum frame_error e=checked_enter(p);if (e) return e;
    uint32_t i;e=lookup(p,id,owner,&i);
    if (!e && p->records[i].role!=expected) e=FRAME_WRONG_ROLE;
    if (!e) role(p,i,FRAME_RETIRING);
    return leave(p,e);
}
enum frame_error frame_pool_reclaim(struct frame_pool *p,uint32_t *released) {
    if (!released) return FRAME_BAD_ARGUMENT;
    enum frame_error e=checked_enter(p);if (e) return e;
    uint32_t count=p->roles[FRAME_RETIRING];
    if (count) {
        /* Callers have already unlinked live mappings. Flush before FREE/reuse.
         * Permanent privileged pool aliases deliberately remain present. */
        p->platform.flush(p->platform.opaque);
        __asm__ volatile("":::"memory");
        for (uint32_t i=0;i<p->selection.managed_count;++i)
            if (p->records[i].role==FRAME_RETIRING) role(p,i,FRAME_FREE);
    }
    *released=count;return leave(p,FRAME_OK);
}
enum frame_error frame_pool_alias(struct frame_pool *p,struct frame_id id,uint64_t owner,volatile unsigned char **out) {
    if (!out) return FRAME_BAD_ARGUMENT;
    enum frame_error e=checked_enter(p);if (e) return e;
    uint32_t i;e=lookup(p,id,owner,&i);
    if (!e && p->records[i].role==FRAME_RETIRING) e=FRAME_WRONG_ROLE;
    if (!e) {
        volatile unsigned char *a=p->platform.alias(p->platform.opaque,p->selection.frames[i]);
        if (!a) e=FRAME_CORRUPT;else *out=a;
    }
    return leave(p,e);
}
enum frame_error frame_pool_fail_after(struct frame_pool *p,uint32_t nth) {
    enum frame_error e=checked_enter(p);if (e) return e;
    p->fail_nth=nth;p->allocation_attempt=0;return leave(p,FRAME_OK);
}

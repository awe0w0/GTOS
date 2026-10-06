#include "sparse_vm.h"
#define NX FRAME_POOL_NX
#define ADDR UINT64_C(0x000ffffffffff000)
static uint64_t next_space_id;
static void zero(void *p,size_t n) { unsigned char *b=p;for(size_t i=0;i<n;++i)b[i]=0; }
__attribute__((noreturn)) static void corrupt(void) { __builtin_trap(); }
static unsigned shift(unsigned kind) { return kind==FRAME_PT?21:kind==FRAME_PD?30:39; }
static int index_of(const struct frame_pool *p,uint64_t physical) {
    uint32_t lo=0,hi=p->selection.managed_count;
    while(lo<hi) { uint32_t m=lo+(hi-lo)/2;uint64_t a=p->selection.frames[m];
        if(a==physical)return (int)m;
        if(a<physical)lo=m+1;else hi=m; }
    return -1;
}
static const struct vm_backing *owned(const struct vm_space *s,uint64_t entry,unsigned kind,uint64_t prefix) {
    int i=index_of(s->pool,entry&ADDR);if(i<0)return 0;
    const struct vm_backing *r=&s->backing[i];
    const struct frame_record *f=&s->pool->records[i];
    if(!r->alias || (uintptr_t)r->alias%sizeof(uint64_t) || r->kind!=kind || r->va!=prefix || r->retiring || f->role!=kind ||
        f->owner!=s->space_id || f->generation!=r->id.generation ||
        r->id.physical!=s->pool->selection.frames[i] ||
        (volatile unsigned char *)r->alias!=s->pool->platform.alias(s->pool->platform.opaque,r->id.physical)) return 0;
    return r;
}
/* Audit only reachable dynamic pages, after proving physical ownership. Do not
 * call frame_pool APIs here: this is also a pool context callback. */
static int hierarchy(const struct vm_space *s,volatile uint64_t *table,unsigned kind,uint64_t parent) {
    for(unsigned j=0;j<512;++j) {
        uint64_t e=table[j];if(!e)continue;
        uint64_t prefix=(parent<<9)|j;
        if(kind==FRAME_DATA) {
            uint64_t va=prefix<<12;
            const struct vm_backing *r=owned(s,e,FRAME_DATA,va);
            if(!r || r->permission==VM_NONE || r->permission>VM_READ_WRITE ||
                (e&~UINT64_C(0x60))!=(r->id.physical|NX|1|(r->permission==VM_READ_WRITE?2:0)))return 0;
        } else {
            const struct vm_backing *r=owned(s,e,kind,prefix);
            if(!r || (e&~UINT64_C(0x20))!=(r->id.physical|NX|3))return 0;
            if(!hierarchy(s,r->alias,kind==FRAME_PT?FRAME_DATA:kind-1,prefix))return 0;
        }
    }
    return 1;
}
int vm_owned_hierarchy_valid(const struct vm_space *s) {
    if(!s || s->ready!=1 || !s->pool || s->pool->service_owner!=s || !s->root || s->root!=s->pool->service_root || !s->space_id ||
        s->pool->selection.managed_count>BOOT_MEMORY_MAX_FRAMES)return 0;
    for(unsigned j=1;j<512;++j) {
        uint64_t e=s->root[j];
        if(j<288 || j>=304) { if(e)return 0;continue; }
        if(!e)continue;
        /* Sign extension is part of the prefix identity, not table indexing. */
        uint64_t prefix=(VM_ARENA_START>>39)+(j-288);
        const struct vm_backing *r=owned(s,e,FRAME_PDPT,prefix);
        if(!r || (e&~UINT64_C(0x20))!=(r->id.physical|NX|3) || !hierarchy(s,r->alias,FRAME_PD,prefix))return 0;
    }
    return 1;
}
static struct vm_backing *find(struct vm_space *s,unsigned kind,uint64_t va,int stages) {
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        ++s->operation_visits;
        if(s->backing[i].kind==kind && s->backing[i].va==va)return &s->backing[i];
    }
    if(stages)for(uint32_t i=0;i<s->staged_count;++i)
        if(s->staging[i].record.kind==kind && s->staging[i].record.va==va)return &s->staging[i].record;
    return 0;
}
static volatile uint64_t *leaf(struct vm_space *s,uint64_t va) {
    struct vm_backing *r=find(s,FRAME_PT,va>>21,0);
    if(!r)corrupt();
    return &r->alias[(va>>12)&511];
}
static uint64_t leaf_value(const struct vm_backing *r) {
    return r->permission==VM_NONE?0:r->id.physical|NX|1|(r->permission==VM_READ_WRITE?2:0);
}
static int stable(struct vm_space *s) {
    if(!vm_owned_hierarchy_valid(s) || s->staged_count)return 0;
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        struct vm_backing *r=&s->backing[i];struct frame_record *f=&s->pool->records[i];
        if(r->retiring)return 0;
        if(!r->kind) { if(f->role!=FRAME_FREE)return 0;continue; }
        if(!owned(s,r->id.physical,r->kind,r->va))return 0;
        if(r->kind==FRAME_DATA) {
            if(r->slot>=VM_MAX_REGIONS)return 0;
            struct vm_slot *v=&s->regions[r->slot];
            if(!v->live || v->generation!=r->generation || r->va<v->base || r->va-v->base>=v->length ||
                (r->va&4095) || r->permission>VM_READ_WRITE)return 0;
            struct vm_backing *t=find(s,FRAME_PT,r->va>>21,0);if(!t)return 0;
            if((t->alias[(r->va>>12)&511]&~UINT64_C(0x60))!=leaf_value(r))return 0;
        } else {
            if(r->kind<FRAME_PT || r->kind>FRAME_PDPT)return 0;
            volatile uint64_t *parent;
            if(r->kind==FRAME_PDPT)parent=&s->root[r->va&511];
            else { struct vm_backing *t=find(s,r->kind+1,r->va>>9,0);if(!t)return 0;parent=&t->alias[r->va&511]; }
            if((*parent&~UINT64_C(0x20))!=(r->id.physical|NX|3))return 0;
            unsigned references=0;
            for(uint32_t k=0;k<s->pool->selection.managed_count;++k) {
                struct vm_backing *c=&s->backing[k];
                if(r->kind==FRAME_PT ? c->kind==FRAME_DATA && c->va>>21==r->va :
                    c->kind==r->kind-1 && c->va>>9==r->va)++references;
            }
            if(!references)return 0;
        }
        for(uint32_t k=0;k<i;++k)if(s->backing[k].kind==r->kind && s->backing[k].va==r->va)return 0;
    }
    for(unsigned i=0;i<VM_MAX_REGIONS;++i)if(s->regions[i].live) {
        struct vm_slot *r=&s->regions[i];
        if(!r->generation || !r->length || (r->base&4095) || (r->length&4095) ||
            r->base<VM_ARENA_START || r->base>=VM_ARENA_END || r->length>VM_ARENA_END-r->base)return 0;
        for(unsigned j=0;j<i;++j)if(s->regions[j].live && r->base<s->regions[j].base+s->regions[j].length &&
            s->regions[j].base<r->base+r->length)return 0;
    }
    return 1;
}
static enum vm_error enter(struct vm_space *s) {
    if(!s || s->ready!=1 || s->busy)return VM_STATE;
    s->busy=1;
    enum frame_error e=frame_pool_audit(s->pool);
    if(e || !stable(s)) { s->busy=0;return e==FRAME_BAD_CONTEXT?VM_STATE:VM_CORRUPT; }
    return VM_OK;
}
static enum vm_error leave(struct vm_space *s,enum vm_error e) { s->busy=0;return e; }
static enum vm_error handle(struct vm_space *s,struct vm_handle h,struct vm_slot **out) {
    if(h.space_id!=s->space_id)return VM_FOREIGN;
    if(h.slot>=VM_MAX_REGIONS)return VM_STALE;
    struct vm_slot *r=&s->regions[h.slot];
    if(!r->live || !h.generation || r->generation!=h.generation)return VM_STALE;
    *out=r;return VM_OK;
}
static enum vm_error range(struct vm_slot *r,uint64_t offset,uint64_t bytes) {
    if(!bytes)return VM_ARGUMENT;
    if((offset|bytes)&4095)return VM_ALIGNMENT;
    if(offset>=r->length || bytes>r->length-offset)return VM_RANGE;
    return VM_OK;
}
static void flush(struct vm_space *s) {
    s->pool->platform.flush(s->pool->platform.opaque);
    if(s->flush_epoch==UINT64_MAX)corrupt();
    ++s->flush_epoch;
}
enum vm_error vm_init(struct vm_space *s,struct frame_pool *p,volatile uint64_t *root) {
    if(!s || !p || !root || (uintptr_t)root%4096)return VM_ARGUMENT;
    if(s->ready || s->busy || p->service_owner || !p->service_root || root!=p->service_root)return VM_STATE;
    if(frame_pool_audit(p)!=FRAME_OK)return VM_STATE;
    if(p->roles[FRAME_FREE]!=p->selection.managed_count)return VM_STATE;
    for(unsigned i=1;i<512;++i)if(root[i])return VM_CONFLICT;
    if(next_space_id==UINT64_MAX)return VM_ID_EXHAUSTED;
    zero(s,sizeof(*s));s->pool=p;s->root=root;s->space_id=++next_space_id;p->service_owner=s;s->ready=1;
    return VM_OK;
}
static int overlap(struct vm_space *s,uint64_t base,uint64_t bytes,uint64_t *next) {
    int found=0;
    for(unsigned i=0;i<VM_MAX_REGIONS;++i) { struct vm_slot *r=&s->regions[i];++s->operation_visits;
        if(r->live && base<r->base+r->length && r->base<base+bytes) {
            if(*next<r->base+r->length)*next=r->base+r->length;
            found=1;
        }
    }
    return found;
}
static int aligned(uint64_t a,uint64_t alignment,uint64_t *out) {
    if(a>UINT64_MAX-(alignment-1))return 0;
    *out=(a+alignment-1)&~(alignment-1);return 1;
}
enum vm_error vm_reserve(struct vm_space *s,uint64_t bytes,uint64_t alignment,
    enum vm_placement placement,uint64_t exact,struct vm_region *out) {
    if(!out || !bytes)return VM_ARGUMENT;
    if(bytes&4095 || alignment<4096 || (alignment&(alignment-1)))return VM_ALIGNMENT;
    if(placement!=VM_ANYWHERE && placement!=VM_EXACT)return VM_UNSUPPORTED;
    if(bytes>VM_ARENA_END-VM_ARENA_START)return VM_RANGE;
    enum vm_error e=enter(s);if(e)return e;
    unsigned slot=VM_MAX_REGIONS;
    for(unsigned i=0;i<VM_MAX_REGIONS;++i)if(!s->regions[i].live && s->regions[i].generation!=UINT64_MAX) { slot=i;break; }
    if(slot==VM_MAX_REGIONS)return leave(s,VM_LIMIT);
    uint64_t base=exact,next=0;
    if(placement==VM_EXACT) {
        if(base&(alignment-1))return leave(s,VM_ALIGNMENT);
        if(base<VM_ARENA_START || base>=VM_ARENA_END || bytes>VM_ARENA_END-base)return leave(s,VM_RANGE);
        if(overlap(s,base,bytes,&next))return leave(s,VM_CONFLICT);
    } else {
        if(!aligned(VM_ARENA_START,alignment,&base))return leave(s,VM_RANGE);
        for(;;) {
            if(base<VM_ARENA_START || base>=VM_ARENA_END || bytes>VM_ARENA_END-base)return leave(s,VM_CONFLICT);
            next=base;
            if(!overlap(s,base,bytes,&next))break;
            if(!aligned(next,alignment,&base))return leave(s,VM_CONFLICT);
        }
    }
    struct vm_slot *r=&s->regions[slot];r->base=base;r->length=bytes;++r->generation;r->live=1;
    *out=(struct vm_region){{s->space_id,r->generation,slot},base,bytes};return leave(s,VM_OK);
}
static enum vm_error allocate(struct vm_space *s,unsigned kind,uint64_t va,struct vm_handle h,enum vm_perm perm) {
    if(s->staged_count==VM_MAX_STAGED)return VM_LIMIT;
    struct frame_id id;
    enum frame_error e=frame_pool_allocate(s->pool,kind==FRAME_DATA?FRAME_STAGED_DATA:FRAME_STAGED_TABLE,s->space_id,&id);
    if(e)return e==FRAME_INJECTED?VM_INJECTED:e==FRAME_EXHAUSTED?VM_NO_FRAMES:e==FRAME_ID_EXHAUSTED?VM_ID_EXHAUSTED:VM_CORRUPT;
    volatile unsigned char *p;
    if(frame_pool_alias(s->pool,id,s->space_id,&p)!=FRAME_OK)corrupt();
    int index=index_of(s->pool,id.physical);if(index<0)corrupt();
    struct vm_stage *v=&s->staging[s->staged_count++];
    v->record=(struct vm_backing){.id=id,.alias=(volatile uint64_t *)p,.va=va,.generation=h.generation,
        .slot=h.slot,.kind=kind,.permission=perm};v->index=(uint32_t)index;
    if(s->staged_count>s->peak_staged)s->peak_staged=s->staged_count;
    return VM_OK;
}
static void rollback(struct vm_space *s) {
    for(uint32_t i=0;i<s->staged_count;++i) {
        if(frame_pool_cancel(s->pool,s->staging[i].record.id,s->space_id)!=FRAME_OK)corrupt();
        zero(&s->staging[i],sizeof(s->staging[i]));
    }
    s->staged_count=0;
}
enum vm_error vm_commit(struct vm_space *s,struct vm_handle h,uint64_t offset,uint64_t bytes,enum vm_perm perm) {
    if(perm!=VM_READ && perm!=VM_READ_WRITE)return VM_UNSUPPORTED;
    enum vm_error e=enter(s);if(e)return e;
    struct vm_slot *r;e=handle(s,h,&r);if(e)return leave(s,e);
    e=range(r,offset,bytes);if(e)return leave(s,e);
    if(bytes/4096>VM_MAX_COMMIT_PAGES)return leave(s,VM_LIMIT);
    uint64_t start=r->base+offset,end=start+bytes;
    uint32_t needed=0;
    uint64_t previous[3]={0,0,0};
    for(uint64_t va=start;va<end;va+=4096) {
        if(!find(s,FRAME_DATA,va,0))++needed;
        for(unsigned kind=FRAME_PT;kind<=FRAME_PDPT;++kind) {
            uint64_t prefix=va>>shift(kind);
            if(previous[kind-FRAME_PT]!=prefix) {
                previous[kind-FRAME_PT]=prefix;
                if(!find(s,kind,prefix,0))++needed;
            }
        }
    }
    if(needed>VM_MAX_STAGED)return leave(s,VM_LIMIT);
    if(needed>s->pool->roles[FRAME_FREE])return leave(s,VM_NO_FRAMES);
    if(needed>UINT64_MAX-s->pool->generation)return leave(s,VM_ID_EXHAUSTED);
    for(uint64_t va=start;va<end;va+=4096) {
        if(!find(s,FRAME_DATA,va,0)) { e=allocate(s,FRAME_DATA,va,h,perm);if(e)goto failed; }
        for(unsigned kind=FRAME_PT;kind<=FRAME_PDPT;++kind)
            if(!find(s,kind,va>>shift(kind),1)) { e=allocate(s,kind,va>>shift(kind),h,VM_NONE);if(e)goto failed; }
    }
    /* All fallible allocations have finished. Promotions cannot fail in a
     * valid serialized transaction; an invariant failure is fatal. */
    for(uint32_t i=0;i<s->staged_count;++i) {
        struct vm_stage *v=&s->staging[i];
        if(frame_pool_promote(s->pool,v->record.id,s->space_id,(enum frame_role)v->record.kind)!=FRAME_OK)corrupt();
    }
    for(uint32_t i=0;i<s->staged_count;++i)s->backing[s->staging[i].index]=s->staging[i].record;
    /* Child entries before parent publication. NX supervisor-only 4KiB. */
    for(uint64_t va=start;va<end;va+=4096) {
        struct vm_backing *d=find(s,FRAME_DATA,va,0);if(!d)corrupt();d->permission=perm;*leaf(s,va)=leaf_value(d);
    }
    for(unsigned kind=FRAME_PT;kind<=FRAME_PDPT;++kind)for(uint32_t i=0;i<s->staged_count;++i) {
        struct vm_backing *t=&s->staging[i].record;if(t->kind!=kind)continue;
        volatile uint64_t *parent;
        if(kind==FRAME_PDPT)parent=&s->root[t->va&511];
        else { struct vm_backing *p=find(s,kind+1,t->va>>9,0);if(!p)corrupt();parent=&p->alias[t->va&511]; }
        __asm__ volatile("":::"memory");*parent=t->id.physical|NX|3;
    }
    for(uint32_t i=0;i<s->staged_count;++i)zero(&s->staging[i],sizeof(s->staging[i]));
    s->staged_count=0;flush(s);
    if(!stable(s))corrupt();
    return leave(s,VM_OK);
failed:
    rollback(s);return leave(s,e);
}
enum vm_error vm_protect(struct vm_space *s,struct vm_handle h,uint64_t offset,uint64_t bytes,enum vm_perm perm) {
    if((unsigned)perm>VM_READ_WRITE)return VM_UNSUPPORTED;
    enum vm_error e=enter(s);if(e)return e;struct vm_slot *r;
    e=handle(s,h,&r);if(e)return leave(s,e);e=range(r,offset,bytes);if(e)return leave(s,e);
    uint64_t start=r->base+offset,end=start+bytes,count=0;
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        struct vm_backing *d=&s->backing[i];++s->operation_visits;
        if(d->kind==FRAME_DATA && d->va>=start && d->va<end)++count;
    }
    if(perm!=VM_NONE && count!=bytes/4096)return leave(s,VM_UNBACKED);
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        struct vm_backing *d=&s->backing[i];
        if(d->kind==FRAME_DATA && d->va>=start && d->va<end) { d->permission=perm;*leaf(s,d->va)=leaf_value(d); }
    }
    if(count)flush(s);
    return leave(s,VM_OK);
}
static void remove_backing(struct vm_space *s,uint64_t start,uint64_t end) {
    uint32_t count=0;
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        struct vm_backing *d=&s->backing[i];++s->operation_visits;
        if(d->kind==FRAME_DATA && d->va>=start && d->va<end) {
#if VM_TEST_INJECT != 1
            *leaf(s,d->va)=0;
#endif
            d->retiring=d->kind;d->kind=0;++count;
        }
    }
    /* Logical backing, including resident NONE, retains its path. Bottom-up
     * pruning unlinks empty tables before ANY pool retirement callback. */
    for(unsigned kind=FRAME_PT;kind<=FRAME_PDPT;++kind)for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        struct vm_backing *t=&s->backing[i];if(t->kind!=kind)continue;
        unsigned refs=0;
        for(uint32_t j=0;j<s->pool->selection.managed_count;++j) {
            struct vm_backing *c=&s->backing[j];++s->operation_visits;
            if(kind==FRAME_PT?c->kind==FRAME_DATA && c->va>>21==t->va:c->kind==kind-1 && c->va>>9==t->va)++refs;
        }
        if(refs)continue;
        volatile uint64_t *parent;
        if(kind==FRAME_PDPT)parent=&s->root[t->va&511];
        else { struct vm_backing *p=find(s,kind+1,t->va>>9,0);if(!p)corrupt();parent=&p->alias[t->va&511]; }
        *parent=0;t->retiring=kind;t->kind=0;++count;
    }
    __asm__ volatile("":::"memory");
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i) {
        struct vm_backing *d=&s->backing[i];
        if(d->retiring && frame_pool_retire(s->pool,d->id,s->space_id,(enum frame_role)d->retiring)!=FRAME_OK)corrupt();
    }
    if(count) {
        uint32_t released=0;
        if(frame_pool_reclaim(s->pool,&released)!=FRAME_OK || released!=count)corrupt();
        if(s->flush_epoch==UINT64_MAX)corrupt();
    ++s->flush_epoch;
    }
    for(uint32_t i=0;i<s->pool->selection.managed_count;++i)if(s->backing[i].retiring)zero(&s->backing[i],sizeof(s->backing[i]));
}
enum vm_error vm_decommit(struct vm_space *s,struct vm_handle h,uint64_t offset,uint64_t bytes) {
    enum vm_error e=enter(s);if(e)return e;struct vm_slot *r;
    e=handle(s,h,&r);if(e)return leave(s,e);e=range(r,offset,bytes);if(e)return leave(s,e);
    remove_backing(s,r->base+offset,r->base+offset+bytes);if(!stable(s))corrupt();
    return leave(s,VM_OK);
}
enum vm_error vm_release(struct vm_space *s,struct vm_handle h) {
    enum vm_error e=enter(s);if(e)return e;struct vm_slot *r;
    e=handle(s,h,&r);if(e)return leave(s,e);
    remove_backing(s,r->base,r->base+r->length);r->live=0;r->base=r->length=0;
    if(!stable(s))corrupt();
    return leave(s,VM_OK);
}
enum vm_error vm_query(struct vm_space *s,struct vm_handle h,uint64_t offset,struct vm_page_state *out) {
    if(!out)return VM_ARGUMENT;
    enum vm_error e=enter(s);if(e)return e;struct vm_slot *r;
    e=handle(s,h,&r);if(e)return leave(s,e);if(offset>=r->length)return leave(s,VM_RANGE);
    struct vm_page_state v={0};struct vm_backing *b=find(s,FRAME_DATA,(r->base+offset)&~UINT64_C(4095),0);
    if(b) { v.backed=1;v.permission=(enum vm_perm)b->permission;v.frame=b->id; }
    *out=v;return leave(s,VM_OK);
}
enum vm_error vm_stats(struct vm_space *s,struct vm_counts *out) {
    if(!out)return VM_ARGUMENT;
    enum vm_error e=enter(s);if(e)return e;struct vm_counts c={0};
    for(unsigned i=0;i<VM_MAX_REGIONS;++i)if(s->regions[i].live) { ++c.reservations;c.reserved_bytes+=s->regions[i].length; }
    c.managed=s->pool->selection.managed_count;c.free_frames=s->pool->roles[FRAME_FREE];
    for(uint32_t i=0;i<c.managed;++i) {
        struct vm_backing *b=&s->backing[i];
        if(b->kind==FRAME_DATA) { ++c.data;if(b->permission==VM_NONE)c.resident_none_bytes+=4096;else c.accessible_bytes+=4096; }
        if(b->kind==FRAME_PT)++c.pt;
        if(b->kind==FRAME_PD)++c.pd;
        if(b->kind==FRAME_PDPT)++c.pdpt;
    }
    c.resident_bytes=c.data*VM_PAGE;c.metadata_bytes=sizeof(*s);c.flush_epoch=s->flush_epoch;
    c.operation_visits=s->operation_visits;c.peak_staged=s->peak_staged;*out=c;return leave(s,VM_OK);
}
enum vm_error vm_audit(struct vm_space *s) { enum vm_error e=enter(s);return e?e:leave(s,VM_OK); }

#include "sparse_vm.h"
#include "fault_probe.h"
#define NX (UINT64_C(1)<<63)
#define PA UINT64_C(0x000ffffffffff000)
#define GIANT UINT64_C(0x158fffff000)
#define MID UINT64_C(0xac00000000)
#define LAST UINT64_C(0x157fffff000)
extern uint64_t pt[];
static struct vm_region slots[VM_MAX_REGIONS];
static struct vm_backing snapshot_backing[BOOT_MEMORY_MAX_FRAMES];
static struct vm_slot snapshot_slots[VM_MAX_REGIONS];
static uint64_t snapshot_root[16],snapshot_tables[32][512];
static uint64_t snapshot_table_pa[32];
static uint32_t snapshot_table_count;
static uint64_t boot_baseline[BOOTINFO_CEILING/4096];
static uint32_t fault_cases,rollback_cases;
static void out(uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"((uint16_t)0xe9)); }
static void say(const char *s) { while (*s) out((uint8_t)*s++); }
static void hex(uint64_t n) { for (int i=60;i>=0;i-=4) out("0123456789abcdef"[(n>>i)&15]); }
__attribute__((noreturn)) static void die(const char *why) {
    say("X64 VM FAIL ");say(why);say("\n");
    __asm__ volatile("outl %0,%1"::"a"(0x11u),"Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("cli; hlt");
}
static void need(int yes,const char *why) { if (!yes) die(why); }
static void good(enum vm_error e,const char *why) { if (e!=VM_OK) { say("X64 VM ERROR code=");hex(e);say("\n");die(why); } }
static void mark(const char *s) { say("X64 VM PASS ");say(s);say("\n"); }
static int equal(const void *a,const void *b,unsigned n) {
    const unsigned char *x=a,*y=b;
    for (unsigned i=0;i<n;++i) if (x[i]!=y[i]) return 0;
    return 1;
}
static void copy(void *a,const void *b,unsigned n) {
    unsigned char *x=a;const unsigned char *y=b;
    for (unsigned i=0;i<n;++i) x[i]=y[i];
}
static struct vm_counts counts(struct vm_space *s) {
    struct vm_counts c;good(vm_stats(s,&c),"stats");return c;
}
static int same_counts(struct vm_counts a,struct vm_counts b) {
    /* Source generations, allocation attempts, visit/flush epochs and peak
     * staging may legitimately advance after rollback. No live state may. */
    return a.reserved_bytes==b.reserved_bytes && a.resident_bytes==b.resident_bytes &&
        a.resident_none_bytes==b.resident_none_bytes && a.accessible_bytes==b.accessible_bytes &&
        a.metadata_bytes==b.metadata_bytes && a.reservations==b.reservations && a.data==b.data &&
        a.pt==b.pt && a.pd==b.pd && a.pdpt==b.pdpt && a.free_frames==b.free_frames && a.managed==b.managed;
}
static unsigned index_of(struct vm_space *s,uint64_t physical) {
    for (unsigned i=0;i<s->pool->selection.managed_count;++i)
        if (s->pool->selection.frames[i]==physical) return i;
    die("page table points outside owned pool");
}
static volatile uint64_t *checked_table(struct vm_space *s,uint64_t entry,unsigned kind,uint64_t prefix) {
    need((entry&~(PA|UINT64_C(0x20)))==(NX|3),"nonleaf exact supervisor RW NX reserved bits");
    unsigned i=index_of(s,entry&PA);
    need(s->backing[i].kind==kind && s->backing[i].va==prefix,"table level prefix ownership");
    need(s->pool->records[i].role==(enum frame_role)kind && s->backing[i].id.physical==(entry&PA),"table physical role");
    return (volatile uint64_t *)(entry&PA);
}
static void audit(struct vm_space *s) {
    struct vm_counts c=counts(s);
    unsigned data=0,none=0,expected[3]={0,0,0},actual[3]={0,0,0},leaves=0;
    for (unsigned i=0;i<c.managed;++i) {
        const struct vm_backing *b=&s->backing[i];
        need(s->pool->records[i].role!=FRAME_STAGED_DATA && s->pool->records[i].role!=FRAME_STAGED_TABLE &&
            s->pool->records[i].role!=FRAME_RETIRING,"no staging or retirement at stable checkpoint");
        if (b->kind!=FRAME_DATA) continue;
        ++data;none+=b->permission==VM_NONE;
        need(b->slot<VM_MAX_REGIONS && s->regions[b->slot].live && b->generation==s->regions[b->slot].generation,
            "data reservation generation owner");
        need(s->regions[b->slot].base<=b->va && b->va-s->regions[b->slot].base<s->regions[b->slot].length,
            "data in owned interval");
        for (unsigned level=0;level<3;++level) {
            unsigned shift=21+level*9,unique=1;
            for (unsigned j=0;j<i;++j) if (s->backing[j].kind==FRAME_DATA &&
                (s->backing[j].va>>shift)==(b->va>>shift)) { unique=0;break; }
            expected[level]+=unique;
        }
    }
    for (unsigned a=288;a<304;++a) {
        if (!s->root[a]) continue;
        uint64_t base=UINT64_C(0xffff000000000000)|((uint64_t)a<<39);
        volatile uint64_t *l3=checked_table(s,s->root[a],FRAME_PDPT,base>>39);++actual[2];
        for (unsigned b=0;b<512;++b) {
            if (!l3[b]) continue;
            uint64_t v3=base|((uint64_t)b<<30);
            volatile uint64_t *l2=checked_table(s,l3[b],FRAME_PD,v3>>30);++actual[1];
            for (unsigned d=0;d<512;++d) {
                if (!l2[d]) continue;
                uint64_t v2=v3|((uint64_t)d<<21);
                volatile uint64_t *l1=checked_table(s,l2[d],FRAME_PT,v2>>21);++actual[0];
                for (unsigned e=0;e<512;++e) {
                    uint64_t v=v2|((uint64_t)e<<12),want=0;
                    for (unsigned i=0;i<c.managed;++i) if (s->backing[i].kind==FRAME_DATA && s->backing[i].va==v) {
                        const struct vm_backing *r=&s->backing[i];
                        if (r->permission!=VM_NONE) want=r->id.physical|NX|1|(r->permission==VM_READ_WRITE?2:0);
                        break;
                    }
                    need((l1[e]&~UINT64_C(0x60))==want,"independent leaf policy or stale mapping");
                    leaves+=(want!=0);
                }
            }
        }
    }
    need(data==c.data && leaves+none==data && c.resident_bytes==(uint64_t)data*VM_PAGE &&
        c.resident_none_bytes==(uint64_t)none*VM_PAGE && c.accessible_bytes==(uint64_t)leaves*VM_PAGE,"independent resident accounting");
    need(actual[0]==expected[0] && actual[1]==expected[1] && actual[2]==expected[2] &&
        c.pt==actual[0] && c.pd==actual[1] && c.pdpt==actual[2],"distinct-prefix table formula");
    need(c.managed==c.free_frames+c.data+c.pt+c.pd+c.pdpt,"physical conservation");
    need(!s->staged_count,"staging cleared");
    good(vm_audit(s),"service own audit");
}
static void zeros(uint64_t base,uint64_t bytes) {
    volatile unsigned char *p=(volatile unsigned char *)base;
    for (uint64_t i=0;i<bytes;++i) need(p[i]==0,"zero-before-use");
}
static unsigned char pattern(uint64_t i,unsigned seed) { return (unsigned char)(1+(i*29+seed)%251); }
static void fill(uint64_t base,uint64_t bytes,unsigned seed) {
    volatile unsigned char *p=(volatile unsigned char *)base;
    for (uint64_t i=0;i<bytes;++i) p[i]=pattern(i,seed);
}
static void verify(uint64_t base,uint64_t bytes,unsigned seed) {
    volatile unsigned char *p=(volatile unsigned char *)base;
    for (uint64_t i=0;i<bytes;++i) need(p[i]==pattern(i,seed),"preserved pattern or owner isolation");
}
static void probe(uint64_t a,unsigned error,unsigned op,const char *label) {
    unsigned before=fault_probe_count();
    void *rip=op==2?(void *)a:op==1?(void *)probe_write_ip:(void *)probe_read_ip;
    void *resume=op==2?(void *)probe_execute_resume:op==1?(void *)probe_write_resume:(void *)probe_read_resume;
    fault_probe_arm(14,error,a,rip,resume,0);
#if VM_TEST_INJECT >= 11 && VM_TEST_INJECT <= 16
    fault_probe_corrupt(VM_TEST_INJECT);
#endif
    if (op==2) probe_execute((void *)a);
    else if (op==1) probe_write((void *)a);
    else probe_read((void *)a);
    fault_probe_check(before+1);++fault_cases;
    say("X64 VM PROBE ");say(label);say("\n");
}
static struct vm_region reserve(struct vm_space *s,uint64_t base,uint64_t length) {
    struct vm_region r;good(vm_reserve(s,length,VM_PAGE,VM_EXACT,base,&r),"exact reserve");return r;
}
static void empty(struct vm_space *s) {
    struct vm_counts c=counts(s);
    need(c.reservations==0 && !c.reserved_bytes && !c.data && !c.pt && !c.pd && !c.pdpt && c.free_frames==c.managed,"complete cleanup accounting");
    for (unsigned i=288;i<304;++i) need(!s->root[i],"complete cleanup high roots");
    audit(s);
}
static void snapshot(struct vm_space *s);
static void same_snapshot(struct vm_space *s);
static void outer_unchanged(struct vm_space *s,struct vm_region r,struct vm_counts before) {
    need(same_counts(before,counts(s)),"outer-bound failure accounting unchanged");
    same_snapshot(s);
    verify(r.base,64*VM_PAGE,7);verify(r.base+MID,VM_PAGE,53);verify(r.base+LAST,VM_PAGE,109);
    verify(r.base+r.length-VM_PAGE,VM_PAGE,211);
}
static void outer_end(struct vm_space *s,struct vm_region r) {
    struct vm_counts reference=counts(s);
    uint64_t offset=r.length-VM_PAGE;
    good(vm_commit(s,r.handle,offset,VM_PAGE,VM_READ_WRITE),"actual outermost page commit");
    zeros(r.base+offset,VM_PAGE);fill(r.base+offset,VM_PAGE,211);verify(r.base+offset,VM_PAGE,211);
    /* Unlike LAST (the reference retained endpoint), this is the final page
     * of the complete untrimmed 0x158fffff000 reservation. It shares a PDPT
     * with LAST but adds a distinct PD and PT. The independent prefix oracle
     * derives the same counts from the backed VA set, not these constants. */
    audit(s);
    struct vm_counts temporary=counts(s);
    need(temporary.data==67 && temporary.pt==4 && temporary.pd==4 && temporary.pdpt==3 &&
        temporary.free_frames+78==temporary.managed,"outermost exact 67 plus 11");
    struct vm_page_state first,last;
    good(vm_query(s,r.handle,offset,&first),"outermost first-byte query");
    good(vm_query(s,r.handle,r.length-1,&last),"outermost final-byte query");
    need(first.backed==1 && last.backed==1 && first.permission==VM_READ_WRITE && last.permission==VM_READ_WRITE &&
        first.frame.physical==last.frame.physical && first.frame.generation==last.frame.generation,
        "actual final reserved byte owns same frame");
    snapshot(s);
#define OUTER_REJECT(operation) do { \
    need((operation)==VM_RANGE,"outer bound operation rejected"); \
    outer_unchanged(s,r,temporary); \
} while (0)
    OUTER_REJECT(vm_commit(s,r.handle,r.length,VM_PAGE,VM_READ));
    OUTER_REJECT(vm_protect(s,r.handle,r.length,VM_PAGE,VM_NONE));
    OUTER_REJECT(vm_decommit(s,r.handle,r.length,VM_PAGE));
    OUTER_REJECT(vm_commit(s,r.handle,offset,2*VM_PAGE,VM_READ));
    OUTER_REJECT(vm_protect(s,r.handle,offset,2*VM_PAGE,VM_NONE));
    OUTER_REJECT(vm_decommit(s,r.handle,offset,2*VM_PAGE));
    OUTER_REJECT(vm_commit(s,r.handle,r.length+VM_PAGE,VM_PAGE,VM_READ));
    OUTER_REJECT(vm_protect(s,r.handle,r.length+VM_PAGE,VM_PAGE,VM_NONE));
    OUTER_REJECT(vm_decommit(s,r.handle,r.length+VM_PAGE,VM_PAGE));
    struct vm_page_state sentinel={.backed=17,.permission=(enum vm_perm)19,.frame={23,29}},output=sentinel;
    OUTER_REJECT(vm_query(s,r.handle,r.length,&output));
    need(equal(&output,&sentinel,sizeof(output)),"one-past query output untouched");
    OUTER_REJECT(vm_query(s,r.handle,r.length+VM_PAGE,&output));
    need(equal(&output,&sentinel,sizeof(output)),"beyond-end query output untouched");
#undef OUTER_REJECT
    audit(s);
    good(vm_decommit(s,r.handle,offset,VM_PAGE),"outermost page decommit");
    need(same_counts(reference,counts(s)),"outermost cleanup restores reference 66 plus 9");
    struct vm_page_state absent;
    good(vm_query(s,r.handle,offset,&absent),"outermost decommitted query");
    need(!absent.backed && absent.permission==VM_NONE,"outermost backing reclaimed");
    verify(r.base,64*VM_PAGE,7);verify(r.base+MID,VM_PAGE,53);verify(r.base+LAST,VM_PAGE,109);
    audit(s);
    mark("true outer-end page data=67 pt=4 pd=4 pdpt=3 bounds atomic restore=66+9");
}
static __attribute__((unused)) void giant(struct vm_space *s) {
    struct vm_counts before=counts(s);
    struct vm_region r=reserve(s,VM_ARENA_START,GIANT);
    struct vm_counts c=counts(s);
    need(r.base==VM_ARENA_START && r.length==GIANT && !c.data && !c.pt && !c.pd && !c.pdpt &&
        c.free_frames==before.free_frames && c.reserved_bytes==GIANT,"giant reservation zero physical delta");
    need(c.operation_visits-before.operation_visits<=4*VM_MAX_REGIONS,"giant reserve bounded visits");
    mark("giant exact reservation bytes=00000158fffff000 data=0 tables=0");
    good(vm_commit(s,r.handle,0,64*VM_PAGE,VM_READ_WRITE),"giant first commit");
    good(vm_commit(s,r.handle,MID,VM_PAGE,VM_READ_WRITE),"giant middle commit");
    good(vm_commit(s,r.handle,LAST,VM_PAGE,VM_READ_WRITE),"giant last commit");
#if VM_TEST_INJECT == 3
    *(volatile unsigned char *)r.base=1;
#endif
    zeros(r.base,64*VM_PAGE);zeros(r.base+MID,VM_PAGE);zeros(r.base+LAST,VM_PAGE);
    fill(r.base,64*VM_PAGE,7);fill(r.base+MID,VM_PAGE,53);fill(r.base+LAST,VM_PAGE,109);
    verify(r.base,64*VM_PAGE,7);verify(r.base+MID,VM_PAGE,53);verify(r.base+LAST,VM_PAGE,109);
#if VM_TEST_INJECT == 4
    s->root[288]|=4;
#elif VM_TEST_INJECT == 2
    ++s->pool->roles[FRAME_FREE];
#endif
    audit(s);c=counts(s);
    need(c.data==66 && c.pt==3 && c.pd==3 && c.pdpt==3 && c.free_frames+75==c.managed,"giant exact 66 plus 9");
    mark("distant real zero isolation data=66 pt=3 pd=3 pdpt=3");
    outer_end(s,r);
    probe(r.base-VM_PAGE,0,0,"outside-left");
    probe(r.base+64*VM_PAGE,0,0,"after-first-run");
    probe(r.base+MID-VM_PAGE,0,0,"before-middle");
    probe(r.base+MID+VM_PAGE,0,0,"after-middle");
    probe(r.base+LAST+VM_PAGE,0,0,"after-last");
    probe(r.base+r.length,0,0,"outside-right");
    good(vm_protect(s,r.handle,0,VM_PAGE,VM_READ),"read-only protect");
    probe(r.base,3,1,"read-only-write");verify(r.base,VM_PAGE,7);
    good(vm_protect(s,r.handle,0,VM_PAGE,VM_NONE),"none protect");
    probe(r.base,0,0,"none-read");probe(r.base,2,1,"none-write");
    c=counts(s);need(c.data==66 && c.resident_none_bytes==VM_PAGE,"NONE retains backing");audit(s);
    good(vm_protect(s,r.handle,0,VM_PAGE,VM_READ_WRITE),"restore RW");verify(r.base,VM_PAGE,7);
    *(volatile unsigned char *)r.base=0xc3;
    probe(r.base,17,2,"nx-execution");
    *(volatile unsigned char *)r.base=pattern(0,7);
    good(vm_commit(s,r.handle,0,VM_PAGE,VM_READ),"recommit retains bytes");verify(r.base,VM_PAGE,7);
    good(vm_protect(s,r.handle,0,VM_PAGE,VM_READ_WRITE),"restore recommit RW");
    mark("exact guards R NONE NX and preserved backing");
    good(vm_decommit(s,r.handle,0,64*VM_PAGE),"decommit 64");
    c=counts(s);need(c.data==2 && c.pt==2 && c.pd==2 && c.pdpt==2,"decommit reclaim path");audit(s);
    probe(r.base,0,0,"decommitted-read");
    good(vm_commit(s,r.handle,0,64*VM_PAGE,VM_READ_WRITE),"recommit 64");zeros(r.base,64*VM_PAGE);
    verify(r.base+MID,VM_PAGE,53);verify(r.base+LAST,VM_PAGE,109);
    before=counts(s);
    good(vm_release(s,r.handle),"release giant");
    need(counts(s).operation_visits-before.operation_visits<UINT64_C(1048576),"giant release bounded visits");
    need(vm_commit(s,r.handle,0,VM_PAGE,VM_READ_WRITE)==VM_STALE,"released handle stale");
    probe(r.base,0,0,"released-read");empty(s);
    r=reserve(s,VM_ARENA_START,GIANT);
    good(vm_commit(s,r.handle,UINT64_C(0x1000000000),64*VM_PAGE,VM_READ_WRITE),"64 GiB trace replay");
    zeros(r.base+UINT64_C(0x1000000000),64*VM_PAGE);c=counts(s);
    need(c.data==64 && c.pt==1 && c.pd==1 && c.pdpt==1,"64 GiB trace accounting");audit(s);
    good(vm_release(s,r.handle),"trace replay release");empty(s);
    mark("decommit recommit zero release and 64GiB trace replay");
}
static __attribute__((unused)) void boundaries(struct vm_space *s) {
    static const uint64_t borders[]={UINT64_C(1)<<21,UINT64_C(1)<<30,UINT64_C(1)<<39};
    for (unsigned i=0;i<3;++i) {
        struct vm_region r=reserve(s,VM_ARENA_START+borders[i]-VM_PAGE,2*VM_PAGE);
        good(vm_commit(s,r.handle,0,2*VM_PAGE,VM_READ_WRITE),"boundary commit");zeros(r.base,2*VM_PAGE);
        fill(r.base,2*VM_PAGE,31+i);verify(r.base,2*VM_PAGE,31+i);audit(s);
        struct vm_counts c=counts(s);
        need(c.data==2 && c.pt==2 && c.pd==(i>=1?2u:1u) && c.pdpt==(i==2?2u:1u),"boundary exact table count");
        good(vm_release(s,r.handle),"boundary release");empty(s);
    }
    mark("2MiB 1GiB 512GiB boundary tables");
}
static __attribute__((unused)) void shared_reuse(struct vm_space *s) {
    uint64_t base=VM_ARENA_START+UINT64_C(0x20000000000);
    struct vm_region a=reserve(s,base,VM_PAGE),b=reserve(s,base+VM_PAGE,VM_PAGE);
    good(vm_commit(s,a.handle,0,VM_PAGE,VM_READ_WRITE),"neighbor first");
    good(vm_commit(s,b.handle,0,VM_PAGE,VM_READ_WRITE),"neighbor second");
    fill(a.base,VM_PAGE,41);fill(b.base,VM_PAGE,83);
    struct vm_page_state old;good(vm_query(s,a.handle,0,&old),"reuse old query");
    struct vm_counts c=counts(s);need(c.data==2 && c.pt==1 && c.pd==1 && c.pdpt==1,"shared PT exact");
    good(vm_release(s,a.handle),"release shared neighbor");verify(b.base,VM_PAGE,83);audit(s);
    struct vm_region other=reserve(s,base+2*VM_PAGE,VM_PAGE);
    good(vm_commit(s,other.handle,0,VM_PAGE,VM_READ_WRITE),"other VA physical reuse");zeros(other.base,VM_PAGE);
    struct vm_page_state reused;good(vm_query(s,other.handle,0,&reused),"new owner query");
    need(reused.frame.physical==old.frame.physical && reused.frame.generation!=old.frame.generation,"forced physical reuse identity");
    fill(other.base,VM_PAGE,127);probe(a.base,0,0,"stale-va-read");probe(a.base,2,1,"stale-va-write");
    verify(other.base,VM_PAGE,127);verify(b.base,VM_PAGE,83);
    struct vm_region replacement=reserve(s,a.base,VM_PAGE);
    need(replacement.handle.generation!=a.handle.generation,"VA reuse token");
    need(vm_release(s,a.handle)==VM_STALE,"old token after exact VA reuse");
    good(vm_commit(s,replacement.handle,0,VM_PAGE,VM_READ_WRITE),"new owner commit");zeros(replacement.base,VM_PAGE);
    struct vm_handle forged=b.handle;forged.space_id^=UINT64_C(0x100000000);
    need(vm_release(s,forged)==VM_FOREIGN,"foreign space token");
    forged=b.handle;forged.generation++;
    need(vm_decommit(s,forged,0,VM_PAGE)==VM_STALE,"forged generation");
    struct vm_region unchanged={.handle={17,19,23},.base=29,.length=31},output=unchanged;
    need(vm_reserve(s,2*VM_PAGE,VM_PAGE,VM_EXACT,base,&output)==VM_CONFLICT && equal(&output,&unchanged,sizeof(output)),"partial overlap atomic output");
    good(vm_release(s,replacement.handle),"new owner release");good(vm_release(s,other.handle),"reuse target release");
    verify(b.base,VM_PAGE,83);good(vm_release(s,b.handle),"last shared neighbor release");empty(s);
    mark("shared neighbors stale foreign handles physical VA reuse");
}
static void snapshot(struct vm_space *s) {
    copy(snapshot_backing,s->backing,sizeof(snapshot_backing));copy(snapshot_slots,s->regions,sizeof(snapshot_slots));
    for (unsigned i=0;i<16;++i) snapshot_root[i]=s->root[288+i]&~UINT64_C(0x20);
    snapshot_table_count=0;
    for (unsigned i=0;i<s->pool->selection.managed_count;++i) if (s->backing[i].kind>=FRAME_PT && s->backing[i].kind<=FRAME_PDPT) {
        need(snapshot_table_count<32,"test snapshot capacity");unsigned n=snapshot_table_count++;
        snapshot_table_pa[n]=s->backing[i].id.physical;
        volatile uint64_t *p=(volatile uint64_t *)snapshot_table_pa[n];
        for (unsigned j=0;j<512;++j) snapshot_tables[n][j]=p[j]&~UINT64_C(0x60);
    }
}
static void same_snapshot(struct vm_space *s) {
    need(equal(snapshot_slots,s->regions,sizeof(snapshot_slots)),"rollback reservation metadata exact");
    need(equal(snapshot_backing,s->backing,sizeof(snapshot_backing)),"rollback backing metadata exact");
    for (unsigned i=0;i<16;++i) need(snapshot_root[i]==(s->root[288+i]&~UINT64_C(0x20)),"rollback root exact");
    for (unsigned i=0;i<snapshot_table_count;++i) {
        volatile uint64_t *p=(volatile uint64_t *)snapshot_table_pa[i];
        for (unsigned j=0;j<512;++j) need(snapshot_tables[i][j]==(p[j]&~UINT64_C(0x60)),"rollback existing tables exact");
    }
}
static __attribute__((unused)) void failures(struct vm_space *s) {
    /* 0: fresh path; 1: existing PT; 2: new PT; 3: new PD+PT;
     * 4: crossing two fresh PML4 paths; 5: max 256-page/262-frame staging. */
    static const struct { uint64_t seed,target,bytes;unsigned allocations; } cases[]={
        {UINT64_MAX,0,2*VM_PAGE,5},
        {0,VM_PAGE,2*VM_PAGE,2},
        {0,UINT64_C(1)<<21,2*VM_PAGE,3},
        {0,UINT64_C(1)<<30,2*VM_PAGE,4},
        {UINT64_MAX,(UINT64_C(1)<<39)-VM_PAGE,2*VM_PAGE,8},
        {UINT64_MAX,(UINT64_C(1)<<39)-128*VM_PAGE,256*VM_PAGE,262}
    };
    for (unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k) {
        struct vm_region r=reserve(s,VM_ARENA_START,UINT64_C(1)<<40);
        if (cases[k].seed!=UINT64_MAX) {
            good(vm_commit(s,r.handle,cases[k].seed,VM_PAGE,VM_READ_WRITE),"failure seed commit");fill(r.base,VM_PAGE,157);
        }
        for (unsigned n=1;n<=cases[k].allocations;++n) {
            struct vm_counts before=counts(s);snapshot(s);
            need(frame_pool_fail_after(s->pool,n)==FRAME_OK,"arm real allocation injection");
            need(vm_commit(s,r.handle,cases[k].target,cases[k].bytes,VM_READ_WRITE)==VM_INJECTED,"every exact allocation failure");
            need(frame_pool_fail_after(s->pool,0)==FRAME_OK,"disable real allocation injection");
            need(same_counts(before,counts(s)),"rollback all accounting categories");same_snapshot(s);audit(s);
            if (cases[k].seed!=UINT64_MAX) verify(r.base,VM_PAGE,157);
            ++rollback_cases;
        }
        good(vm_commit(s,r.handle,cases[k].target,cases[k].bytes,VM_READ_WRITE),"same operation succeeds after every failure");
        zeros(r.base+cases[k].target,cases[k].bytes);audit(s);
        if (cases[k].seed!=UINT64_MAX) verify(r.base,VM_PAGE,157);
        good(vm_release(s,r.handle),"failure case cleanup");empty(s);
    }
    mark("every allocation failure atomic new existing and 512GiB paths");
}
static __attribute__((unused)) void bad_ranges(struct vm_space *s) {
    struct vm_region sentinel={.handle={17,19,23},.base=29,.length=31},out_region=sentinel;
    struct vm_counts before=counts(s);
#define BAD_RESERVE(bytes,alignment,placement,base) do { \
    need(vm_reserve(s,bytes,alignment,placement,base,&out_region)!=VM_OK && \
        equal(&sentinel,&out_region,sizeof(sentinel)),"invalid reserve unchanged output"); \
} while (0)
    BAD_RESERVE(0,VM_PAGE,VM_ANYWHERE,0);
    BAD_RESERVE(1,VM_PAGE,VM_ANYWHERE,0);
    BAD_RESERVE(VM_PAGE,3*VM_PAGE,VM_ANYWHERE,0);
    BAD_RESERVE(VM_PAGE,2048,VM_ANYWHERE,0);
    BAD_RESERVE(VM_PAGE,VM_PAGE,VM_EXACT,VM_ARENA_START+1);
    BAD_RESERVE(VM_PAGE,VM_PAGE,VM_EXACT,VM_ARENA_START-VM_PAGE);
    BAD_RESERVE(VM_PAGE,VM_PAGE,VM_EXACT,VM_ARENA_END);
    BAD_RESERVE(UINT64_MAX-4095,VM_PAGE,VM_EXACT,VM_ARENA_START);
    BAD_RESERVE(VM_PAGE,VM_PAGE,VM_EXACT,UINT64_C(0x0000800000000000));
    BAD_RESERVE(UINT64_C(0xffff800000002000),VM_PAGE,VM_EXACT,UINT64_C(0x00007ffffffff000));
    BAD_RESERVE(VM_PAGE,UINT64_C(1)<<63,VM_ANYWHERE,0);
    BAD_RESERVE(VM_PAGE,VM_PAGE,(enum vm_placement)99,0);
#undef BAD_RESERVE
    need(same_counts(before,counts(s)),"invalid reserves no mutation");
    struct vm_region r=reserve(s,VM_ARENA_START,512*VM_PAGE);
    good(vm_commit(s,r.handle,0,VM_PAGE,VM_READ_WRITE),"negative seed");fill(r.base,VM_PAGE,179);
    before=counts(s);snapshot(s);
    need(vm_commit(s,r.handle,0,257*VM_PAGE,VM_READ_WRITE)==VM_LIMIT,"transaction quota");
    need(vm_commit(s,r.handle,0,VM_PAGE,(enum vm_perm)3)==VM_UNSUPPORTED,"executable unsupported");
    need(vm_commit(s,r.handle,0,VM_PAGE,VM_NONE)==VM_UNSUPPORTED,"NONE commit unsupported");
    need(vm_commit(s,r.handle,UINT64_MAX-4095,VM_PAGE,VM_READ_WRITE)!=VM_OK,"offset overflow");
    need(vm_commit(s,r.handle,1,VM_PAGE,VM_READ_WRITE)!=VM_OK,"unaligned commit");
    need(vm_commit(s,r.handle,r.length,VM_PAGE,VM_READ_WRITE)!=VM_OK,"outside commit");
    need(vm_protect(s,r.handle,0,2*VM_PAGE,VM_READ)==VM_UNBACKED,"protect unbacked atomic");
    s->busy=1;need(vm_release(s,r.handle)==VM_STATE,"recursive state rejection");s->busy=0;
    uint64_t cr4;
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    __asm__ volatile("mov %0,%%cr4"::"r"(cr4|UINT64_C(0x80)):"memory");
    enum vm_error context_error=vm_release(s,r.handle);
    __asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
    need(context_error==VM_STATE,"VM genuine unsupported CR4 rejection");
    struct vm_page_state q={.backed=17,.permission=(enum vm_perm)19,.frame={23,29}},q_old=q;
    need(vm_query(s,r.handle,r.length,&q)==VM_RANGE && equal(&q,&q_old,sizeof(q)),"query output untouched on range failure");
    need(same_counts(before,counts(s)),"invalid operation accounting");same_snapshot(s);verify(r.base,VM_PAGE,179);audit(s);
    good(vm_release(s,r.handle),"negative release");
    for (unsigned i=0;i<VM_MAX_REGIONS;++i) slots[i]=reserve(s,VM_ARENA_START+(uint64_t)i*VM_PAGE,VM_PAGE);
    out_region=sentinel;
    need(vm_reserve(s,VM_PAGE,VM_PAGE,VM_ANYWHERE,0,&out_region)==VM_LIMIT && equal(&out_region,&sentinel,sizeof(sentinel)),"reservation quota unchanged output");
    for (unsigned i=0;i<VM_MAX_REGIONS;++i) good(vm_release(s,slots[i].handle),"slot cleanup");
    struct vm_region aligned;good(vm_reserve(s,VM_PAGE,65536,VM_ANYWHERE,0,&aligned),"64K aligned reserve");
    need(!(aligned.base&65535),"64K alignment");good(vm_release(s,aligned.handle),"aligned release");empty(s);
    /* Deliberately put one empty slot at its terminal generation. No live
     * authority is forged; future reserves must permanently skip that slot. */
    s->regions[0].generation=UINT64_MAX;
    good(vm_reserve(s,VM_PAGE,VM_PAGE,VM_ANYWHERE,0,&aligned),"generation retirement reserve");
    need(aligned.handle.slot!=0 && s->regions[0].generation==UINT64_MAX,"generation never wraps into validity");
    good(vm_release(s,aligned.handle),"generation retirement release");empty(s);
    mark("invalid ranges permissions transaction and reservation quotas");
}
static __attribute__((unused)) void exhaustion(struct vm_space *s) {
    struct vm_region r=reserve(s,VM_ARENA_START,16*1024*1024);
    uint64_t offset=0;
    for (;;) {
        struct vm_counts before=counts(s);
        enum vm_error e=vm_commit(s,r.handle,offset,256*VM_PAGE,VM_READ_WRITE);
        if (e==VM_NO_FRAMES) { need(same_counts(before,counts(s)),"natural OOM atomic accounting");break; }
        good(e,"bounded real pool fill");zeros(r.base+offset,256*VM_PAGE);offset+=256*VM_PAGE;
        need(offset<r.length,"bounded pool did not exhaust");
    }
    need(offset!=0,"normal pool meaningful exhaustion");audit(s);
    good(vm_release(s,r.handle),"natural exhaustion cleanup");empty(s);
    mark("real bounded pool exhaustion and recovery");
}
static __attribute__((unused)) void fragments(struct vm_space *s) {
    uint32_t seed=UINT32_C(0xc0ffee);
    for (unsigned round=0;round<8;++round) {
        for (unsigned i=0;i<16;++i) {
            seed=seed*1664525u+1013904223u;
            uint64_t base=VM_ARENA_START+(uint64_t)i*(UINT64_C(1)<<30)+(uint64_t)((seed>>16)&255)*VM_PAGE;
            slots[i]=reserve(s,base,3*VM_PAGE);
            good(vm_commit(s,slots[i].handle,VM_PAGE,VM_PAGE,VM_READ_WRITE),"fragment commit");
            zeros(base+VM_PAGE,VM_PAGE);fill(base+VM_PAGE,VM_PAGE,i+round);
        }
        audit(s);
        for (unsigned n=0;n<16;++n) {
            unsigned i=(n*5)&15;
            verify(slots[i].base+VM_PAGE,VM_PAGE,i+round);
            good(vm_protect(s,slots[i].handle,VM_PAGE,VM_PAGE,VM_NONE),"fragment NONE");
            good(vm_protect(s,slots[i].handle,VM_PAGE,VM_PAGE,VM_READ_WRITE),"fragment restore");
            verify(slots[i].base+VM_PAGE,VM_PAGE,i+round);good(vm_release(s,slots[i].handle),"fragment release");
        }
        empty(s);
    }
    mark("deterministic fragmented ownership lifecycle");
}
/* The lifecycle oracle is intentionally driven by the test's address/permission
 * plan, not by walking the service's backing records. It independently predicts
 * every resident page, every distinct paging prefix, all owned intervals, and
 * every byte (including inaccessible backing through checked pool aliases). */
struct expected_page { uint64_t va,pattern_offset; enum vm_perm permission; unsigned seed; };
static uint32_t lifecycle_rollbacks;
static struct expected_page expected(uint64_t va,enum vm_perm permission,unsigned seed) {
    struct expected_page p={va,0,permission,seed};return p;
}
static void oracle(struct vm_space *s,const struct vm_region *regions,unsigned nr,
    const struct expected_page *pages,unsigned np) {
    struct vm_counts c=counts(s);uint64_t reserved=0;unsigned prefixes[3]={0,0,0},none=0;
    for(unsigned i=0;i<nr;++i) {
        reserved+=regions[i].length;
        const struct vm_slot *slot=&s->regions[regions[i].handle.slot];
        need(slot->live && slot->base==regions[i].base && slot->length==regions[i].length &&
            slot->generation==regions[i].handle.generation && regions[i].handle.space_id==s->space_id,
            "expected exact region authority");
        for(unsigned j=0;j<i;++j)need(regions[j].handle.slot!=regions[i].handle.slot,"oracle duplicate region");
    }
    for(unsigned i=0;i<np;++i) {
        unsigned owner=nr;
        for(unsigned j=0;j<nr;++j)if(pages[i].va>=regions[j].base &&
            pages[i].va-regions[j].base<regions[j].length) { need(owner==nr,"oracle overlapping ownership");owner=j; }
        need(owner<nr,"oracle page has expected owner");
        struct vm_page_state q;good(vm_query(s,regions[owner].handle,pages[i].va-regions[owner].base,&q),"oracle query");
        need(q.backed && q.permission==pages[i].permission,"oracle expected page and exact permission");
        none+=q.permission==VM_NONE;
        unsigned frame=index_of(s,q.frame.physical);
        need(s->pool->records[frame].role==FRAME_DATA && s->pool->records[frame].generation==q.frame.generation,
            "oracle checked physical data alias");
        volatile unsigned char *bytes=(volatile unsigned char *)(q.permission==VM_NONE?q.frame.physical:pages[i].va);
        for(unsigned b=0;b<VM_PAGE;++b)need(bytes[b]==(pages[i].seed?pattern(pages[i].pattern_offset+b,pages[i].seed):0),
            "oracle all bytes preserved or zero");
        for(unsigned level=0;level<3;++level) {
            unsigned unique=1,shift=21+9*level;
            for(unsigned j=0;j<i;++j) {
                need(pages[j].va!=pages[i].va,"oracle duplicate page");
                if((pages[j].va>>shift)==(pages[i].va>>shift))unique=0;
            }
            prefixes[level]+=unique;
        }
    }
    need(c.reservations==nr && c.reserved_bytes==reserved && c.data==np &&
        c.resident_bytes==(uint64_t)np*VM_PAGE && c.resident_none_bytes==(uint64_t)none*VM_PAGE &&
        c.accessible_bytes==(uint64_t)(np-none)*VM_PAGE,"independent lifecycle region and page counts");
    need(c.pt==prefixes[0] && c.pd==prefixes[1] && c.pdpt==prefixes[2] &&
        c.free_frames+np+prefixes[0]+prefixes[1]+prefixes[2]==c.managed,
        "independent lifecycle unique-prefix frame counts");
    audit(s);
}
static struct vm_page_state page(struct vm_space *s,struct vm_region r,uint64_t off) {
    struct vm_page_state q;good(vm_query(s,r.handle,off,&q),"lifecycle page query");return q;
}
static void absent(struct vm_space *s,struct vm_region r,uint64_t off) {
    struct vm_page_state q=page(s,r,off);need(!q.backed && q.permission==VM_NONE,"lifecycle absence remains unbacked");
}
static void fresh(struct vm_region old,struct vm_region replacement) {
    need(replacement.handle.space_id==old.handle.space_id &&
        (replacement.handle.slot!=old.handle.slot || replacement.handle.generation!=old.handle.generation),
        "lifecycle consumes old token and returns fresh authority");
}
static void bounded(struct vm_space *s,struct vm_counts before) {
    need(counts(s).operation_visits-before.operation_visits<UINT64_C(1048576),"lifecycle bounded by metadata not VA pages");
}
static void unchanged(struct vm_space *s,struct vm_counts before) {
    need(same_counts(before,counts(s)) && counts(s).flush_epoch==before.flush_epoch,
        "lifecycle preflight failure leaves counts and translations unchanged");same_snapshot(s);
}
static void bad_authority(struct vm_space *s,struct vm_handle h,enum vm_error error) {
    struct vm_region r,old_r;struct vm_regions rs,old_rs;struct vm_disposition d,old_d;
    struct vm_page_state q,old_q;
    fill((uint64_t)&old_r,sizeof(old_r),43);fill((uint64_t)&old_rs,sizeof(old_rs),59);
    fill((uint64_t)&old_d,sizeof(old_d),71);fill((uint64_t)&old_q,sizeof(old_q),83);
    copy(&r,&old_r,sizeof(r));copy(&rs,&old_rs,sizeof(rs));copy(&d,&old_d,sizeof(d));copy(&q,&old_q,sizeof(q));
    struct vm_counts before=counts(s);snapshot(s);
    need(vm_commit(s,h,0,VM_PAGE,VM_READ)==error,"bad authority commit");
    need(vm_protect(s,h,0,VM_PAGE,VM_NONE)==error,"bad authority protect");
    need(vm_decommit(s,h,0,VM_PAGE)==error,"bad authority decommit");
    need(vm_discard(s,h,0,VM_PAGE,&d)==error,"bad authority discard");
    need(vm_reset(s,h,0,VM_PAGE,&r)==error,"bad authority reset");
    need(vm_trim(s,h,0,VM_PAGE,&r)==error,"bad authority trim");
    need(vm_split(s,h,VM_PAGE,&rs)==error,"bad authority split");
    need(vm_punch(s,h,0,VM_PAGE,&rs)==error,"bad authority punch");
    need(vm_query(s,h,0,&q)==error && vm_release(s,h)==error,"bad authority query release");
    need(equal(&r,&old_r,sizeof(r)) && equal(&rs,&old_rs,sizeof(rs)) &&
        equal(&d,&old_d,sizeof(d)) && equal(&q,&old_q,sizeof(q)),"bad authority all outputs unchanged");
    unchanged(s,before);
}
static __attribute__((unused)) void lifecycle_discard(struct vm_space *s) {
    uint64_t base=VM_ARENA_START+UINT64_C(0x30000000000);
    struct vm_region regions[2]={reserve(s,base,6*VM_PAGE),reserve(s,base+6*VM_PAGE,VM_PAGE)};
    static const unsigned indices[]={0,1,2,4,5,6};
    struct expected_page p[7];
    for(unsigned i=0;i<6;++i) {
        unsigned n=indices[i];struct vm_region r=regions[n==6];uint64_t off=n==6?0:(uint64_t)n*VM_PAGE;
        good(vm_commit(s,r.handle,off,VM_PAGE,VM_READ_WRITE),"discard seed commit");fill(base+n*VM_PAGE,VM_PAGE,11+n);
        enum vm_perm perm=n==0?VM_READ:(n==2 || n==5?VM_NONE:VM_READ_WRITE);
        good(vm_protect(s,r.handle,off,VM_PAGE,perm),"discard seed permission");p[i]=expected(base+n*VM_PAGE,perm,11+n);
    }
    oracle(s,regions,2,p,6);
    struct vm_page_state old_r=page(s,regions[0],0),old_rw=page(s,regions[0],VM_PAGE);
    struct vm_disposition d={99,101};struct vm_counts before=counts(s);
    good(vm_discard(s,regions[0].handle,0,4*VM_PAGE,&d),"mixed discard");bounded(s,before);
    need(d.zeroed_pages==2 && d.released_pages==1,"discard precise zero versus reclaim counts");
    struct vm_page_state new_r=page(s,regions[0],0),new_rw=page(s,regions[0],VM_PAGE);
    need(new_r.frame.physical==old_r.frame.physical && new_r.frame.generation==old_r.frame.generation &&
        new_rw.frame.physical==old_rw.frame.physical && new_rw.frame.generation==old_rw.frame.generation,
        "accessible discard zeros in original physical frames");
    p[0].seed=p[1].seed=0;for(unsigned i=2;i<5;++i)p[i]=p[i+1];oracle(s,regions,2,p,5);
    absent(s,regions[0],2*VM_PAGE);absent(s,regions[0],3*VM_PAGE);
    probe(base,3,1,"discard-retained-R-write");
    probe(base+2*VM_PAGE,0,0,"discard-released-NONE-read");
    probe(base+3*VM_PAGE,2,1,"discard-unbacked-write");
    fill(base+VM_PAGE,VM_PAGE,77);p[1].seed=77;oracle(s,regions,2,p,5);
    good(vm_commit(s,regions[0].handle,2*VM_PAGE,2*VM_PAGE,VM_READ_WRITE),"discard reclaimed and absent recommit");
    zeros(base+2*VM_PAGE,2*VM_PAGE);p[5]=expected(base+2*VM_PAGE,VM_READ_WRITE,0);p[6]=expected(base+3*VM_PAGE,VM_READ_WRITE,0);
    oracle(s,regions,2,p,7);
    good(vm_discard(s,regions[0].handle,2*VM_PAGE,2*VM_PAGE,&d),"accessible repeated discard");
    need(d.zeroed_pages==2 && !d.released_pages,"repeated accessible discard does not claim reclaim");oracle(s,regions,2,p,7);
    good(vm_release(s,regions[0].handle),"discard outer release");verify(regions[1].base,VM_PAGE,17);
    good(vm_release(s,regions[1].handle),"discard neighbor release");empty(s);
    mark("lifecycle mixed discard R RW NONE absent exact zero release permissions");
}
static __attribute__((unused)) void lifecycle_trace(struct vm_space *s) {
    const uint64_t kept=UINT64_C(0x15800000000),reset_bytes=UINT64_C(0x100000000);
    struct vm_region r=reserve(s,VM_ARENA_START,GIANT),next;
    struct vm_counts unbacked=counts(s);
    need(!unbacked.data && !unbacked.pt && !unbacked.pd && !unbacked.pdpt && unbacked.reserved_bytes==GIANT,
        "reference unbacked giant reserve exact zero frames");
    good(vm_trim(s,r.handle,0,kept,&next),"reference unbacked giant suffix trim");bounded(s,unbacked);fresh(r,next);
    need(next.base==r.base && next.length==kept && counts(s).free_frames==unbacked.free_frames,
        "reference unbacked trim exact interval zero physical delta");
    bad_authority(s,r.handle,VM_STALE);oracle(s,&next,1,0,0);
    good(vm_release(s,next.handle),"unbacked reference trim cleanup");empty(s);
    mark("lifecycle unbacked giant suffix trim zero data tables physical delta");
    r=reserve(s,VM_ARENA_START,GIANT);
    struct expected_page p[68];
    good(vm_commit(s,r.handle,MID,64*VM_PAGE,VM_READ_WRITE),"lifecycle distant 256KiB MID commit");zeros(r.base+MID,64*VM_PAGE);
    fill(r.base+MID,64*VM_PAGE,37);
    for(unsigned i=0;i<64;++i) { p[i]=expected(r.base+MID+i*VM_PAGE,VM_READ_WRITE,37);p[i].pattern_offset=i*VM_PAGE; }
    const uint64_t offsets[]={0,kept-VM_PAGE,kept,GIANT-VM_PAGE};
    for(unsigned i=0;i<4;++i) {
        good(vm_commit(s,r.handle,offsets[i],VM_PAGE,VM_READ_WRITE),"trim distant seed");
        fill(r.base+offsets[i],VM_PAGE,131+i);p[64+i]=expected(r.base+offsets[i],VM_READ_WRITE,131+i);
    }
    good(vm_protect(s,r.handle,0,VM_PAGE,VM_READ),"trim retain R");p[64].permission=VM_READ;
    good(vm_protect(s,r.handle,kept-VM_PAGE,VM_PAGE,VM_NONE),"trim retain NONE");p[65].permission=VM_NONE;
    oracle(s,&r,1,p,68);struct vm_counts before=counts(s);
    good(vm_trim(s,r.handle,0,kept,&next),"giant exact suffix trim");bounded(s,before);fresh(r,next);
    need(next.base==r.base && next.length==kept,"trace exact giant retained extent");bad_authority(s,r.handle,VM_STALE);r=next;
    oracle(s,&r,1,p,66);
    probe(r.base+kept,0,0,"trim-giant-suffix-read");probe(r.base+GIANT-VM_PAGE,2,1,"trim-giant-outer-write");
    mark("lifecycle giant trim 00000158fffff000 to 0000015800000000 distant-MID=000000ac00000000 pages=64");
    /* 0xac00000000 above is deliberate distant stress. The observed Linux
     * reference commits at 0x1000000000 (64 GiB), independently replayed here. */
    const uint64_t trace=UINT64_C(0x1000000000);
    good(vm_decommit(s,r.handle,MID,64*VM_PAGE),"clear distinct distant-midpoint stress");
    good(vm_commit(s,r.handle,trace,64*VM_PAGE,VM_READ_WRITE),"reference retained-giant 64GiB commit");
    zeros(r.base+trace,64*VM_PAGE);fill(r.base+trace,64*VM_PAGE,37);
    for(unsigned i=0;i<64;++i)p[i].va=r.base+trace+i*VM_PAGE;
    oracle(s,&r,1,p,66);
    good(vm_protect(s,r.handle,trace,64*VM_PAGE,VM_NONE),"reference NONE before DONTNEED-equivalent discard");
    for(unsigned i=0;i<64;++i)p[i].permission=VM_NONE;
    oracle(s,&r,1,p,66);
    struct vm_disposition disposition;
    good(vm_discard(s,r.handle,trace,64*VM_PAGE,&disposition),"reference inaccessible 256KiB discard");
    need(!disposition.zeroed_pages && disposition.released_pages==64,"reference discard really returns 64 frames");
    struct expected_page survivors[2]={p[64],p[65]};oracle(s,&r,1,survivors,2);
    probe(r.base+trace,0,0,"trace-NONE-discard-read");
    good(vm_commit(s,r.handle,trace,64*VM_PAGE,VM_READ_WRITE),"reference discard zero recommit");
    for(unsigned i=0;i<64;++i) { p[i].permission=VM_READ_WRITE;p[i].seed=0; }
    oracle(s,&r,1,p,66);
    const uint64_t sparse_offsets[2]={trace+(UINT64_C(1)<<31),trace+reset_bytes-VM_PAGE};
    for(unsigned i=0;i<2;++i) {
        good(vm_commit(s,r.handle,sparse_offsets[i],VM_PAGE,VM_READ_WRITE),"owned 4GiB reset distant sparse backing");
        fill(r.base+sparse_offsets[i],VM_PAGE,151+i);
        p[66+i]=expected(r.base+sparse_offsets[i],i?VM_NONE:VM_READ_WRITE,151+i);
        if(i)good(vm_protect(s,r.handle,sparse_offsets[i],VM_PAGE,VM_NONE),"owned 4GiB reset distant NONE backing");
    }
    oracle(s,&r,1,p,68);
    before=counts(s);good(vm_reset(s,r.handle,trace,reset_bytes,&next),"owned four GiB reset");bounded(s,before);fresh(r,next);
    need(next.base==r.base && next.length==r.length,"reset rotates whole outer authority");bad_authority(s,r.handle,VM_STALE);r=next;
    oracle(s,&r,1,survivors,2);
    absent(s,r,trace);absent(s,r,trace+(UINT64_C(1)<<31));absent(s,r,trace+reset_bytes-VM_PAGE);
    probe(r.base+trace,0,0,"reset-owned-4GiB-read");probe(r.base+trace+63*VM_PAGE,2,1,"reset-owned-4GiB-write");
    probe(r.base+sparse_offsets[0],0,0,"reset-owned-4GiB-sparse-middle-read");
    probe(r.base+sparse_offsets[1],2,1,"reset-owned-4GiB-sparse-last-write");
    good(vm_commit(s,r.handle,trace,64*VM_PAGE,VM_READ_WRITE),"reset zero on recommit");
    for(unsigned i=0;i<64;++i)p[i].seed=0;
    oracle(s,&r,1,p,66);
    good(vm_release(s,r.handle),"lifecycle trace cleanup");empty(s);
    mark("lifecycle reference 64GiB commit NONE discard sparse 4GiB reset outside R NONE zero recommit");
}
static __attribute__((unused)) void lifecycle_alignment(struct vm_space *s) {
    const uint64_t total=UINT64_C(134279168),prefix=UINT64_C(45056),keep=UINT64_C(134217728),suffix=UINT64_C(16384);
    uint64_t base=VM_ARENA_START+UINT64_C(0x40000000000)+UINT64_C(0x5000);
    struct vm_region r=reserve(s,base,total),next;
    const uint64_t off[]={0,prefix,prefix+keep-VM_PAGE,prefix+keep};struct expected_page p[4];
    for(unsigned i=0;i<4;++i) {
        good(vm_commit(s,r.handle,off[i],VM_PAGE,VM_READ_WRITE),"alignment seed commit");
        fill(base+off[i],VM_PAGE,149+i);p[i]=expected(base+off[i],VM_READ_WRITE,149+i);
    }
    good(vm_protect(s,r.handle,prefix,VM_PAGE,VM_READ),"alignment retained R");p[1].permission=VM_READ;
    good(vm_protect(s,r.handle,prefix+keep-VM_PAGE,VM_PAGE,VM_NONE),"alignment retained NONE");p[2].permission=VM_NONE;
    oracle(s,&r,1,p,4);struct vm_counts before=counts(s);
    good(vm_trim(s,r.handle,prefix,keep,&next),"trace two-sided alignment trim");bounded(s,before);fresh(r,next);
    need(total==prefix+keep+suffix && next.base==base+prefix && next.length==keep && !(next.base&65535),
        "134279168 minus 45056 minus 16384 is 128MiB aligned 64KiB");
    bad_authority(s,r.handle,VM_STALE);r=next;oracle(s,&r,1,p+1,2);
    probe(base,0,0,"trim-alignment-prefix-read");probe(base+prefix+keep,2,1,"trim-alignment-suffix-write");
    struct vm_region regions[3]={r,reserve(s,base,prefix),reserve(s,base+prefix+keep,suffix)};
    good(vm_commit(s,regions[1].handle,0,VM_PAGE,VM_READ_WRITE),"trim prefix reusable hole");
    good(vm_commit(s,regions[2].handle,0,VM_PAGE,VM_READ_WRITE),"trim suffix reusable hole");
    p[0].seed=p[3].seed=0;oracle(s,regions,3,p,4);
    /* The fresh owner remains live; removed holes belong only to their new tokens. */
    for(unsigned i=0;i<3;++i)good(vm_release(s,regions[i].handle),"alignment retained and holes cleanup");
    empty(s);
    mark("lifecycle alignment reserve=134279168 prefix=45056 suffix=16384 retained=134217728 align=65536");
}
static __attribute__((unused)) void lifecycle_partition(struct vm_space *s) {
    uint64_t base=VM_ARENA_START+UINT64_C(0x50000000000)+(UINT64_C(1)<<21)-2*VM_PAGE;
    struct vm_region original=reserve(s,base,8*VM_PAGE);
    struct vm_region left_neighbor=reserve(s,base-VM_PAGE,VM_PAGE),right_neighbor=reserve(s,base+8*VM_PAGE,VM_PAGE);
    struct expected_page p[11];
    for(unsigned i=0;i<8;++i) {
        good(vm_commit(s,original.handle,i*VM_PAGE,VM_PAGE,VM_READ_WRITE),"partition seed");fill(base+i*VM_PAGE,VM_PAGE,101+i);
        enum vm_perm perm=i%3==0?VM_READ:i%3==1?VM_READ_WRITE:VM_NONE;
        good(vm_protect(s,original.handle,i*VM_PAGE,VM_PAGE,perm),"partition permissions");p[i]=expected(base+i*VM_PAGE,perm,101+i);
    }
    good(vm_commit(s,left_neighbor.handle,0,VM_PAGE,VM_READ_WRITE),"left external neighbor");fill(left_neighbor.base,VM_PAGE,201);
    good(vm_commit(s,right_neighbor.handle,0,VM_PAGE,VM_READ_WRITE),"right external neighbor");fill(right_neighbor.base,VM_PAGE,211);
    p[8]=expected(left_neighbor.base,VM_READ_WRITE,201);p[9]=expected(right_neighbor.base,VM_READ_WRITE,211);
    struct vm_region regions[7]={original,left_neighbor,right_neighbor};oracle(s,regions,3,p,10);
    struct vm_handle foreign=original.handle;foreign.space_id^=UINT64_C(0x4000000000000000);bad_authority(s,foreign,VM_FOREIGN);
    struct vm_regions split;struct vm_counts before=counts(s);
    good(vm_split(s,original.handle,4*VM_PAGE,&split),"split exact half");bounded(s,before);
    need(split.count==2 && split.regions[0].base==base && split.regions[0].length==4*VM_PAGE &&
        split.regions[1].base==base+4*VM_PAGE && split.regions[1].length==4*VM_PAGE,"split two exact nonoverlapping halves");
    fresh(original,split.regions[0]);fresh(original,split.regions[1]);bad_authority(s,original.handle,VM_STALE);
    regions[0]=split.regions[0];regions[1]=split.regions[1];regions[2]=left_neighbor;regions[3]=right_neighbor;
    oracle(s,regions,4,p,10);need(same_counts(before,counts(s))==0,"split increases owned-record count");
    struct vm_region out,out_before;struct vm_regions outs,outs_before;struct vm_disposition disposition,disposition_before;
    fill((uint64_t)&out_before,sizeof(out_before),17);fill((uint64_t)&outs_before,sizeof(outs_before),23);
    fill((uint64_t)&disposition_before,sizeof(disposition_before),29);
    copy(&out,&out_before,sizeof(out));copy(&outs,&outs_before,sizeof(outs));copy(&disposition,&disposition_before,sizeof(disposition));
    before=counts(s);snapshot(s);
    need(vm_discard(s,regions[0].handle,4*VM_PAGE,VM_PAGE,&disposition)==VM_RANGE,"left discard cannot enter right owner");
    need(vm_reset(s,regions[0].handle,3*VM_PAGE,2*VM_PAGE,&out)==VM_RANGE,"left reset cannot span right owner");
    need(vm_trim(s,regions[0].handle,0,5*VM_PAGE,&out)==VM_RANGE,"left trim cannot grow into right owner");
    need(vm_punch(s,regions[1].handle,UINT64_MAX-VM_PAGE+1,VM_PAGE,&outs)==VM_RANGE,"right punch cannot reach left neighbor");
    need(equal(&out,&out_before,sizeof(out)) && equal(&outs,&outs_before,sizeof(outs)) &&
        equal(&disposition,&disposition_before,sizeof(disposition)),"neighbor-authority rejection outputs untouched");
    unchanged(s,before);oracle(s,regions,4,p,10);
    struct vm_page_state removed1=page(s,regions[0],VM_PAGE),removed2=page(s,regions[0],2*VM_PAGE);
    struct vm_regions punch;good(vm_punch(s,regions[0].handle,VM_PAGE,2*VM_PAGE,&punch),"interior punch two retained intervals");
    need(punch.count==2 && punch.regions[0].base==base && punch.regions[0].length==VM_PAGE &&
        punch.regions[1].base==base+3*VM_PAGE && punch.regions[1].length==VM_PAGE,"interior punch exact output holes");
    fresh(regions[0],punch.regions[0]);fresh(regions[0],punch.regions[1]);bad_authority(s,regions[0].handle,VM_STALE);
    struct vm_region right=regions[1];regions[0]=punch.regions[0];regions[1]=punch.regions[1];regions[2]=right;
    regions[3]=left_neighbor;regions[4]=right_neighbor;
    struct expected_page remain[11];unsigned np=0;
    for(unsigned i=0;i<10;++i)if(i!=1 && i!=2)remain[np++]=p[i];
    oracle(s,regions,5,remain,np);
    probe(base+VM_PAGE,0,0,"punch-interior-accessible-read");probe(base+2*VM_PAGE,2,1,"punch-interior-NONE-write");
    probe(base,3,1,"split-punch-retained-R-write");
    struct vm_region reused=reserve(s,base+9*VM_PAGE,VM_PAGE);
    good(vm_commit(s,reused.handle,0,VM_PAGE,VM_READ_WRITE),"punch released frame new VA owner");zeros(reused.base,VM_PAGE);
    struct vm_page_state new_page=page(s,reused,0);
    need((new_page.frame.physical==removed1.frame.physical && new_page.frame.generation!=removed1.frame.generation) ||
        (new_page.frame.physical==removed2.frame.physical && new_page.frame.generation!=removed2.frame.generation),
        "punch removed data frame really reused at other VA");
    fill(reused.base,VM_PAGE,223);regions[5]=reused;remain[np++]=expected(reused.base,VM_READ_WRITE,223);oracle(s,regions,6,remain,np);
    probe(base+VM_PAGE,0,0,"punch-stale-after-physical-reuse-read");probe(base+2*VM_PAGE,2,1,"punch-stale-after-physical-reuse-write");
    good(vm_punch(s,right.handle,0,VM_PAGE,&punch),"prefix edge punch");
    need(punch.count==1 && punch.regions[0].base==base+5*VM_PAGE && punch.regions[0].length==3*VM_PAGE,"prefix punch exact remaining interval");
    fresh(right,punch.regions[0]);bad_authority(s,right.handle,VM_STALE);right=punch.regions[0];regions[2]=right;
    np=0;for(unsigned i=0;i<10;++i)if(i!=1 && i!=2 && i!=4)remain[np++]=p[i];remain[np++]=expected(reused.base,VM_READ_WRITE,223);
    oracle(s,regions,6,remain,np);probe(base+4*VM_PAGE,0,0,"punch-prefix-read");
    good(vm_punch(s,right.handle,2*VM_PAGE,VM_PAGE,&punch),"suffix edge punch");
    need(punch.count==1 && punch.regions[0].base==base+5*VM_PAGE && punch.regions[0].length==2*VM_PAGE,"suffix punch exact remaining interval");
    fresh(right,punch.regions[0]);bad_authority(s,right.handle,VM_STALE);right=punch.regions[0];regions[2]=right;
    np=0;for(unsigned i=0;i<10;++i)if(i!=1 && i!=2 && i!=4 && i!=7)remain[np++]=p[i];remain[np++]=expected(reused.base,VM_READ_WRITE,223);
    oracle(s,regions,6,remain,np);probe(base+7*VM_PAGE,2,1,"punch-suffix-write");
    good(vm_fail_metadata_after(s,1),"arm unreachable output stage for full punch");
    good(vm_punch(s,right.handle,0,right.length,&punch),"full punch needs zero output records");
    good(vm_fail_metadata_after(s,0),"disable full punch staging failure");need(!punch.count,"full punch returns zero retained intervals");
    bad_authority(s,right.handle,VM_STALE);regions[2]=left_neighbor;regions[3]=right_neighbor;regions[4]=reused;
    remain[0]=p[0];remain[1]=p[3];remain[2]=p[8];remain[3]=p[9];remain[4]=expected(reused.base,VM_READ_WRITE,223);
    oracle(s,regions,5,remain,5);probe(base+5*VM_PAGE,0,0,"punch-full-NONE-read");probe(base+6*VM_PAGE,0,0,"punch-full-R-read");
    struct vm_region hole=reserve(s,base+VM_PAGE,2*VM_PAGE);regions[5]=hole;
    good(vm_commit(s,hole.handle,0,2*VM_PAGE,VM_READ_WRITE),"interior punched hole exact VA reuse");
    remain[5]=expected(hole.base,VM_READ_WRITE,0);remain[6]=expected(hole.base+VM_PAGE,VM_READ_WRITE,0);oracle(s,regions,6,remain,7);
    bad_authority(s,original.handle,VM_STALE);good(vm_release(s,hole.handle),"hole cleanup");
    for(unsigned i=0;i<5;++i)if(i!=3)good(vm_release(s,regions[i].handle),"partition survivor cleanup");
    oracle(s,&right_neighbor,1,p+9,1);good(vm_release(s,right_neighbor.handle),"last shared table reference cleanup");empty(s);
    mark("lifecycle split interior edge full punch holes stale foreign neighbor authority shared table reclaim");
}
static enum vm_error metadata_operation(struct vm_space *s,unsigned op,struct vm_handle h,
    struct vm_region *r,struct vm_regions *rs) {
    switch(op) {
        case 0:return vm_reset(s,h,2*VM_PAGE,3*VM_PAGE,r);
        case 1:return vm_trim(s,h,VM_PAGE,6*VM_PAGE,r);
        case 2:return vm_split(s,h,4*VM_PAGE,rs);
        case 3:return vm_punch(s,h,0,2*VM_PAGE,rs);
        case 4:return vm_punch(s,h,6*VM_PAGE,2*VM_PAGE,rs);
        default:return vm_punch(s,h,2*VM_PAGE,3*VM_PAGE,rs);
    }
}
static __attribute__((unused)) void lifecycle_metadata(struct vm_space *s) {
    for(unsigned op=0;op<6;++op) {
        uint64_t base=VM_ARENA_START+UINT64_C(0x60000000000);
        struct vm_region original=reserve(s,base,8*VM_PAGE),neighbor=reserve(s,base+8*VM_PAGE,VM_PAGE);
        struct vm_region regions[3]={original,neighbor};struct expected_page p[9];
        for(unsigned i=0;i<9;++i) {
            struct vm_region owner=i==8?neighbor:original;uint64_t off=i==8?0:i*VM_PAGE;
            good(vm_commit(s,owner.handle,off,VM_PAGE,VM_READ_WRITE),"metadata failure byte seed");fill(base+i*VM_PAGE,VM_PAGE,61+i);
            enum vm_perm perm=i%3==0?VM_NONE:i%3==1?VM_READ:VM_READ_WRITE;
            good(vm_protect(s,owner.handle,off,VM_PAGE,perm),"metadata failure permission seed");p[i]=expected(base+i*VM_PAGE,perm,61+i);
        }
        oracle(s,regions,2,p,9);
        struct vm_region out,old_out;struct vm_regions outs,old_outs;
        fill((uint64_t)&old_out,sizeof(old_out),97);fill((uint64_t)&old_outs,sizeof(old_outs),103);
        unsigned stages=(op==2 || op==5)?2:1;
        for(unsigned n=1;n<=stages;++n) {
            copy(&out,&old_out,sizeof(out));copy(&outs,&old_outs,sizeof(outs));
            good(vm_fail_metadata_after(s,n),"arm every metadata output stage");struct vm_counts before=counts(s);snapshot(s);
            need(metadata_operation(s,op,original.handle,&out,&outs)==VM_INJECTED,"exact metadata staging rejection");
            unchanged(s,before);bounded(s,before);
            need(equal(&out,&old_out,sizeof(out)) && equal(&outs,&old_outs,sizeof(outs)),"metadata failure output bytes untouched");
            good(vm_fail_metadata_after(s,0),"disable metadata output injection");oracle(s,regions,2,p,9);++lifecycle_rollbacks;
        }
        /* nth counts actual output records: the next unvisited stage cannot fail. */
        good(vm_fail_metadata_after(s,stages+1),"arm nonexistent output stage");
        good(metadata_operation(s,op,original.handle,&out,&outs),"same operation recovers after all metadata failures");
        good(vm_fail_metadata_after(s,0),"disable nonexistent staging index");bad_authority(s,original.handle,VM_STALE);
        unsigned nr=0;if(op<2)regions[nr++]=out;else for(unsigned i=0;i<outs.count;++i)regions[nr++]=outs.regions[i];
        regions[nr++]=neighbor;struct expected_page retained[9];unsigned np=0;
        for(unsigned i=0;i<9;++i) {
            int keep= i==8 || op==2 || (op==0?(i<2 || i>=5):op==1?(i>=1 && i<7):
                op==3?i>=2:op==4?i<6:(i<2 || i>=5));
            if(keep)retained[np++]=p[i];
        }
        for(unsigned i=0;i+1<nr;++i)fresh(original,regions[i]);
        oracle(s,regions,nr,retained,np);
        for(unsigned i=0;i<nr;++i)good(vm_release(s,regions[i].handle),"metadata failure case cleanup");
        empty(s);
    }
    need(lifecycle_rollbacks==8,"every one and two output metadata stage exercised");
    mark("lifecycle metadata staging rollback=8 outputs bytes permissions tables exact recovery");
}
static __attribute__((unused)) void lifecycle_invalid(struct vm_space *s) {
    uint64_t base=VM_ARENA_START+UINT64_C(0x70000000000);
    struct vm_region r=reserve(s,base,4*VM_PAGE);struct expected_page p[2];
    good(vm_commit(s,r.handle,0,2*VM_PAGE,VM_READ_WRITE),"invalid lifecycle byte seed");
    fill(base,VM_PAGE,173);fill(base+VM_PAGE,VM_PAGE,179);
    good(vm_protect(s,r.handle,VM_PAGE,VM_PAGE,VM_NONE),"invalid lifecycle NONE seed");
    p[0]=expected(base,VM_READ_WRITE,173);p[1]=expected(base+VM_PAGE,VM_NONE,179);
    struct vm_region out,old_out;struct vm_regions outs,old_outs;struct vm_disposition d,old_d;
    fill((uint64_t)&old_out,sizeof(old_out),13);fill((uint64_t)&old_outs,sizeof(old_outs),17);fill((uint64_t)&old_d,sizeof(old_d),19);
    static const struct {uint64_t off,bytes;} invalid[]={
        {0,0},{1,VM_PAGE},{0,1},{4*VM_PAGE,VM_PAGE},{3*VM_PAGE,2*VM_PAGE},
        {UINT64_MAX-VM_PAGE+1,VM_PAGE},{VM_PAGE,UINT64_MAX-VM_PAGE+1}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        copy(&out,&old_out,sizeof(out));copy(&outs,&old_outs,sizeof(outs));copy(&d,&old_d,sizeof(d));
        struct vm_counts before=counts(s);snapshot(s);
        need(vm_discard(s,r.handle,invalid[i].off,invalid[i].bytes,&d)!=VM_OK,"invalid discard range");
        need(vm_reset(s,r.handle,invalid[i].off,invalid[i].bytes,&out)!=VM_OK,"invalid reset range");
        need(vm_trim(s,r.handle,invalid[i].off,invalid[i].bytes,&out)!=VM_OK,"invalid trim range");
        need(vm_punch(s,r.handle,invalid[i].off,invalid[i].bytes,&outs)!=VM_OK,"invalid punch range");
        need(equal(&out,&old_out,sizeof(out)) && equal(&outs,&old_outs,sizeof(outs)) && equal(&d,&old_d,sizeof(d)),
            "invalid lifecycle range outputs exact");unchanged(s,before);oracle(s,&r,1,p,2);
    }
    struct vm_counts before=counts(s);snapshot(s);copy(&outs,&old_outs,sizeof(outs));
    need(vm_split(s,r.handle,0,&outs)!=VM_OK && vm_split(s,r.handle,1,&outs)!=VM_OK &&
        vm_split(s,r.handle,r.length,&outs)!=VM_OK && vm_split(s,r.handle,r.length+VM_PAGE,&outs)!=VM_OK &&
        vm_split(s,r.handle,UINT64_MAX,&outs)!=VM_OK && equal(&outs,&old_outs,sizeof(outs)),"invalid split no empty halves or overflow");
    need(vm_discard(s,r.handle,0,VM_PAGE,0)==VM_ARGUMENT && vm_reset(s,r.handle,0,VM_PAGE,0)==VM_ARGUMENT &&
        vm_trim(s,r.handle,0,VM_PAGE,0)==VM_ARGUMENT && vm_split(s,r.handle,VM_PAGE,0)==VM_ARGUMENT &&
        vm_punch(s,r.handle,0,VM_PAGE,0)==VM_ARGUMENT,"null lifecycle outputs rejected before mutation");
    unchanged(s,before);oracle(s,&r,1,p,2);
    good(vm_release(s,r.handle),"invalid lifecycle cleanup");empty(s);
    mark("lifecycle invalid zero alignment overflow empty split and null outputs atomic");
}
static unsigned next_available(struct vm_space *s) {
    for(unsigned i=0;i<VM_MAX_REGIONS;++i)if(!s->regions[i].live && s->regions[i].generation!=UINT64_MAX)return i;
    die("generation fixture needs a nonterminal vacant slot");
}
static __attribute__((unused)) void lifecycle_limits(struct vm_space *s) {
    uint64_t base=VM_ARENA_START+UINT64_C(0x70000000000);
    unsigned terminal=next_available(s);s->regions[terminal].generation=UINT64_MAX-1;
    struct vm_region r=reserve(s,base,4*VM_PAGE),next;
    need(r.handle.slot==terminal && r.handle.generation==UINT64_MAX,"terminal live authority fixture");
    good(vm_commit(s,r.handle,0,VM_PAGE,VM_READ_WRITE),"terminal migration preserve seed");fill(base,VM_PAGE,191);
    good(vm_commit(s,r.handle,2*VM_PAGE,VM_PAGE,VM_READ_WRITE),"terminal migration reset seed");fill(base+2*VM_PAGE,VM_PAGE,193);
    good(vm_reset(s,r.handle,2*VM_PAGE,VM_PAGE,&next),"terminal generation migrates to fresh slot");
    need(next.handle.slot!=terminal && s->regions[terminal].generation==UINT64_MAX && !s->regions[terminal].live,
        "terminal old slot permanently retired rather than wrapped");
    fresh(r,next);bad_authority(s,r.handle,VM_STALE);struct expected_page p=expected(base,VM_READ_WRITE,191);oracle(s,&next,1,&p,1);
    good(vm_release(s,next.handle),"terminal migration cleanup");empty(s);
    terminal=next_available(s);s->regions[terminal].generation=UINT64_MAX-1;
    r=reserve(s,base,4*VM_PAGE);need(r.handle.slot==terminal && r.handle.generation==UINT64_MAX,"full quota terminal owner fixture");
    good(vm_commit(s,r.handle,0,VM_PAGE,VM_READ_WRITE),"quota accessible bytes");fill(base,VM_PAGE,197);
    good(vm_commit(s,r.handle,3*VM_PAGE,VM_PAGE,VM_READ_WRITE),"quota inaccessible bytes");fill(base+3*VM_PAGE,VM_PAGE,199);
    good(vm_protect(s,r.handle,3*VM_PAGE,VM_PAGE,VM_NONE),"quota NONE backing");
    struct vm_region regions[VM_MAX_REGIONS];regions[0]=r;unsigned nr=1;
    for(unsigned i=0;i<VM_MAX_REGIONS;++i)if(!s->regions[i].live && s->regions[i].generation!=UINT64_MAX) {
        regions[nr]=reserve(s,base+(uint64_t)(nr+4)*VM_PAGE,VM_PAGE);++nr;
    }
    struct expected_page pages[2]={expected(base,VM_READ_WRITE,197),expected(base+3*VM_PAGE,VM_NONE,199)};
    oracle(s,regions,nr,pages,2);
    struct vm_region out,old_out;struct vm_regions outs,old_outs;
    fill((uint64_t)&old_out,sizeof(old_out),29);fill((uint64_t)&old_outs,sizeof(old_outs),31);
    for(unsigned op=0;op<6;++op) {
        copy(&out,&old_out,sizeof(out));copy(&outs,&old_outs,sizeof(outs));struct vm_counts before=counts(s);snapshot(s);
        enum vm_error e=op==0?vm_reset(s,r.handle,VM_PAGE,VM_PAGE,&out):
            op==1?vm_trim(s,r.handle,VM_PAGE,2*VM_PAGE,&out):op==2?vm_split(s,r.handle,2*VM_PAGE,&outs):
            op==3?vm_punch(s,r.handle,0,VM_PAGE,&outs):op==4?vm_punch(s,r.handle,3*VM_PAGE,VM_PAGE,&outs):
            vm_punch(s,r.handle,VM_PAGE,2*VM_PAGE,&outs);
        need(e==VM_LIMIT,"terminal owner and full metadata quota rejects fresh outputs");
        need(equal(&out,&old_out,sizeof(out)) && equal(&outs,&old_outs,sizeof(outs)),"quota rejection outputs exact");
        unchanged(s,before);bounded(s,before);oracle(s,regions,nr,pages,2);
    }
    good(vm_fail_metadata_after(s,1),"quota full punch does not stage output");
    good(vm_punch(s,r.handle,0,r.length,&outs),"full punch succeeds without generation or slot headroom");
    good(vm_fail_metadata_after(s,0),"quota staging disarm");need(!outs.count,"quota full punch empty output");
    bad_authority(s,r.handle,VM_STALE);oracle(s,regions+1,nr-1,0,0);probe(base,0,0,"terminal-full-punch-read");
    copy(&out,&old_out,sizeof(out));struct vm_counts before=counts(s);snapshot(s);
    need(vm_reserve(s,4*VM_PAGE,VM_PAGE,VM_EXACT,base,&out)==VM_LIMIT && equal(&out,&old_out,sizeof(out)),
        "terminal released slot cannot be recycled into validity");unchanged(s,before);
    need(nr>1,"quota fixture has ordinary slot to release");good(vm_release(s,regions[nr-1].handle),"free nonterminal quota slot");
    next=reserve(s,base,4*VM_PAGE);need(next.handle.slot!=terminal,"VA reused through different nonterminal slot");
    good(vm_commit(s,next.handle,0,VM_PAGE,VM_READ_WRITE),"quota recovery zero recommit");zeros(base,VM_PAGE);
    bad_authority(s,r.handle,VM_STALE);good(vm_release(s,next.handle),"quota recovery cleanup");
    for(unsigned i=1;i+1<nr;++i)good(vm_release(s,regions[i].handle),"quota filler cleanup");
    empty(s);
    mark("lifecycle metadata quota terminal migration no generation wrap full punch zero outputs");
}

void vm_guest_tests(struct vm_space *s) {
    fault_probe_scope_vm();
    for (unsigned i=0;i<BOOTINFO_CEILING/4096;++i) boot_baseline[i]=pt[i]&~UINT64_C(0x60);
    empty(s);
#if FRAME_TEST_SMALL
    struct vm_region r=reserve(s,VM_ARENA_START,8*VM_PAGE);
    struct vm_counts before=counts(s);snapshot(s);
    need(vm_commit(s,r.handle,0,5*VM_PAGE,VM_READ_WRITE)==VM_NO_FRAMES,"reduced pool genuine OOM");
    need(same_counts(before,counts(s)),"reduced pool rollback accounting");same_snapshot(s);audit(s);
    good(vm_commit(s,r.handle,0,4*VM_PAGE,VM_READ_WRITE),"reduced pool exact capacity");zeros(r.base,4*VM_PAGE);audit(s);
    need(counts(s).free_frames==0,"reduced pool completely used");good(vm_release(s,r.handle),"reduced pool release");empty(s);
    mark("reduced real pool seven frames exact OOM recovery");
#else
    giant(s);boundaries(s);shared_reuse(s);bad_ranges(s);failures(s);exhaustion(s);fragments(s);
    need(fault_cases==14 && rollback_cases==284,"original sparse VM evidence unchanged");
    say("X64 VM ORIGINAL PASS faults=14 rollback=284\n");
    lifecycle_discard(s);lifecycle_trace(s);lifecycle_alignment(s);lifecycle_partition(s);
    lifecycle_metadata(s);lifecycle_invalid(s);lifecycle_limits(s);
#endif
    for (unsigned i=0;i<BOOTINFO_CEILING/4096;++i) need(boot_baseline[i]==(pt[i]&~UINT64_C(0x60)),"original mappings unchanged after VM cleanup");
    mark("all dynamic mappings reclaimed original protections unchanged");
    say("X64 VM SERVICE PASS BSP-only data=0 tables=0 live=0 faults=");hex(fault_cases);
    say(" rollback=");hex(rollback_cases);say(" managed=");hex(counts(s).managed);say("\n");
    say("X64 VM LIMITS supervisor NX-only user SMP JIT demand-paging deferred\n");
}

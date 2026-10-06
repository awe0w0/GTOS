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
#endif
    for (unsigned i=0;i<BOOTINFO_CEILING/4096;++i) need(boot_baseline[i]==(pt[i]&~UINT64_C(0x60)),"original mappings unchanged after VM cleanup");
    mark("all dynamic mappings reclaimed original protections unchanged");
    say("X64 VM SERVICE PASS BSP-only data=0 tables=0 live=0 faults=");hex(fault_cases);
    say(" rollback=");hex(rollback_cases);say(" managed=");hex(counts(s).managed);say("\n");
    say("X64 VM LIMITS supervisor NX-only discard reset trim split punch deferred\n");
}

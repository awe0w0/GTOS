#include "frame_pool.h"
#include "frame_boot_tests.h"
#if VM_TEST
#include "sparse_vm.h"
static struct vm_space vm;
extern void vm_guest_tests(struct vm_space *);
#endif
extern uint64_t pml4[],pdpt[],pd[],pt[];
extern unsigned char stack_bottom[],stack_top[],__kernel_start[],__kernel_end[];
static struct frame_pool pool;
static struct frame_id ids[BOOT_MEMORY_MAX_FRAMES];
static uint64_t baseline[BOOTINFO_CEILING/4096];
static uint64_t alternate_root[512] __attribute__((aligned(4096)));
static uint32_t flushes;
static inline void out(uint16_t p,uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static void say(const char *s) { while (*s) out(0xe9,(unsigned char)*s++); }
static void hex(uint64_t n) { for (int i=60;i>=0;i-=4) out(0xe9,"0123456789abcdef"[(n>>i)&15]); }
__attribute__((noreturn)) static void die(const char *s) {
    say("X64 FRAME FAIL ");say(s);say("\n");
    __asm__ volatile("outl %0,%1"::"a"(0x11u),"Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("cli; hlt");
}
static void need(int ok,const char *s) { if (!ok) die(s); }
static uint64_t msr(uint32_t n) { uint32_t a,d;__asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(n));return a|((uint64_t)d<<32); }
static int context(void *opaque) {
    (void)opaque;
    uint64_t c0,c3,c4,flags,sp;uint16_t cs;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2; pushfq; pop %3; mov %%rsp,%4; mov %%cs,%5"
        :"=r"(c0),"=r"(c3),"=r"(c4),"=r"(flags),"=r"(sp),"=r"(cs));
    if (cs!=24 || flags&(1ull<<9) || sp<(uint64_t)stack_bottom || sp>=(uint64_t)stack_top ||
        (c0&0x8001000d)!=0x8001000d || c3!=(uint64_t)pml4 || c4!=0x20 ||
        (msr(0xc0000080)&0xd00)!=0xd00 || !(msr(0x1b)&0x100)) return 0;
    /* Every borrowed intermediate is still the exact boot-owned supervisor tree. */
    if ((pml4[0]&~0x20ull)!=((uint64_t)pdpt|3) ||
        (pdpt[0]&~0x20ull)!=((uint64_t)pd|3)) return 0;
    for (unsigned i=0;i<512;++i) {
        if (i && pdpt[i]) return 0;
#if VM_TEST
        if (i && !vm.ready && pml4[i]) return 0;
#else
        if (i && pml4[i]) return 0;
#endif
        if ((pd[i]&~0x20ull)!=(i<32 ? (uint64_t)&pt[i*512]|3 : 0)) return 0;
    }
#if VM_TEST
    if (vm.ready && (vm.root!=pml4 || !vm_owned_hierarchy_valid(&vm))) return 0;
#endif
    return 1;
}
static uint64_t leaf(void *opaque,uint64_t a) { (void)opaque;return pt[a/4096]; }
static void write_leaf(void *opaque,uint64_t a,uint64_t v) { (void)opaque;pt[a/4096]=v; }
static void flush(void *opaque) {
    (void)opaque;
    __asm__ volatile("mov %0,%%cr3"::"r"(pml4):"memory");
    ++flushes;
}
static volatile unsigned char *alias(void *opaque,uint64_t a) { (void)opaque;return (volatile unsigned char *)a; }
static const struct frame_platform platform={context,leaf,write_leaf,flush,alias,0};
static int same_id(struct frame_id a,struct frame_id b) { return a.physical==b.physical && a.generation==b.generation; }
static void stats_free(uint32_t count) {
    struct frame_stats s;
    need(frame_pool_stats(&pool,&s)==FRAME_OK,"stats audit");
    need(s.managed==count && s.roles[FRAME_FREE]==count && s.permanent_aliases==count && s.borrowed_table_frames==35,"exact free accounting");
    for (unsigned r=1;r<FRAME_ROLE_COUNT;++r) need(s.roles[r]==0,"leaked role");
}
static void whole_map_audit(void) {
    uint32_t next=0;
    for (uint64_t a=0;a<BOOTINFO_CEILING;a+=4096) {
        uint64_t expected=baseline[a/4096];
        if (next<pool.selection.managed_count && a==pool.selection.frames[next]) {
            need(!expected,"alias overwrote baseline leaf");expected=a|FRAME_POOL_NX|3;++next;
        }
        need((pt[a/4096]&~0x60ull)==expected,"whole map delta");
    }
    need(next==pool.selection.managed_count,"alias count");
    need(frame_pool_audit(&pool)==FRAME_OK,"pool audit");
}
static void source_audit(const void *bytes,size_t size,const struct boot_memory_request *request) {
    struct handoff_view view;
    need(!handoff_parse(bytes,size,request->kernel_start,request->kernel_end,&view),"source revalidation");
    uint32_t eligible=0,selected=0;
    for (uint64_t a=BOOT_MEMORY_FLOOR;a<BOOT_MEMORY_CEILING;a+=4096) {
        int usable=0;
        struct handoff_memory_range r;
        for (uint32_t i=0;handoff_memory_range(&view,i,&r);++i)
            if (r.type==1 && r.start<=a && a+4096<=r.end) usable=1;
        if (a<request->kernel_end && request->kernel_start<a+4096) usable=0;
        if (a<request->original_info_start+request->original_info_size && request->original_info_start<a+4096) usable=0;
        uint32_t cursor=0;struct handoff_range x;
        while (handoff_next_exclusion(&view,&cursor,&x)) if (a<x.end && x.start<a+4096) usable=0;
        if (usable) {
            ++eligible;
            if (selected<pool.selection.managed_count)
                need(pool.selection.frames[selected++]==a,"independent selected frame oracle");
        }
    }
    need(eligible==pool.selection.eligible_count && selected==pool.selection.managed_count,"independent eligible count");
    say("X64 FRAME PASS source exclusions modules=");hex(view.module_count);
    say(" framebuffer=");hex(view.framebuffer_offset!=0);say(" first=");hex(pool.selection.frames[0]);
    say(" last=");hex(pool.selection.frames[selected-1]);say("\n");
}
void frame_guest_tests(const void *private_copy,size_t size,uint64_t original) {
    frame_boot_tests(private_copy,size,original,&platform);
    struct boot_memory_request request={
        .kernel_start=(uint64_t)__kernel_start,.kernel_end=(uint64_t)__kernel_end,
        .original_info_start=original,.original_info_size=size,
#if FRAME_TEST_SMALL
        .min_frames=1,.max_frames=7,
#endif
    };
    for (uint32_t i=0;i<BOOTINFO_CEILING/4096;++i) baseline[i]=pt[i]&~0x60ull;
    const char *why=0;
    enum frame_error error=frame_pool_init(&pool,private_copy,size,&request,&platform,&why);
    if (error) { say("X64 FRAME INIT error=");hex(error);say(" reason=");say(why?why:"platform");say("\n");die("initialization"); }
    uint32_t count=pool.selection.managed_count;
    stats_free(count);whole_map_audit();source_audit(private_copy,size,&request);
    say("X64 FRAME PASS ownership firmware=");hex(pool.selection.firmware_usable_bytes);
    say(" eligible=");hex(pool.selection.eligible_count);say(" managed=");hex(count);
    say(" aliases=");hex(count);say(" borrowed-tables=");hex(35);say("\n");
    /* Prime every real owned-pool physical page with nonzero guest writes.
     * This test-only direct privileged access cannot invent candidate frames. */
    for (uint32_t i=0;i<count;++i) {
        volatile unsigned char *p=(volatile unsigned char *)pool.selection.frames[i];
        for (unsigned j=0;j<4096;++j) p[j]=0xa5;
    }
    /* Negative caller authority, genuine CPU state, and no output publication. */
    struct frame_id untouched={0x123,0x456}, result=untouched;
    need(frame_pool_allocate(&pool,FRAME_DATA,1,&result)==FRAME_BAD_ARGUMENT && same_id(result,untouched),"unsupported direct live allocation");
    pool.busy=1;need(frame_pool_allocate(&pool,FRAME_STAGED_DATA,1,&result)==FRAME_BAD_STATE,"recursive entry");pool.busy=0;
    __asm__ volatile("mov %%cr4,%%rax; or $0x80,%%rax; mov %%rax,%%cr4":::"rax","memory");
    error=frame_pool_allocate(&pool,FRAME_STAGED_DATA,1,&result);
    __asm__ volatile("mov $0x20,%%rax; mov %%rax,%%cr4":::"rax","memory");
    need(error==FRAME_BAD_CONTEXT && same_id(result,untouched),"CR4 context");
    for (unsigned i=0;i<512;++i) alternate_root[i]=pml4[i];
    __asm__ volatile("mov %0,%%cr3"::"r"(alternate_root):"memory");
    error=frame_pool_allocate(&pool,FRAME_STAGED_DATA,1,&result);
    flush(0);need(error==FRAME_BAD_CONTEXT && same_id(result,untouched),"CR3 context");
    out(0x21,0xff);out(0xa1,0xff);
    __asm__ volatile("sti; nop":::"memory");
    error=frame_pool_allocate(&pool,FRAME_STAGED_DATA,1,&result);
    __asm__ volatile("cli":::"memory");
    need(error==FRAME_BAD_CONTEXT && same_id(result,untouched),"IF context");
    say("X64 FRAME PASS context BSP IF CR3 CR4 recursion\n");
    /* All classes consume the SAME actual finite RAM pool. Every allocation is
     * read byte-for-byte before writing, then read again after all allocations. */
    uint32_t expected[FRAME_ROLE_COUNT]={0};
    for (uint32_t i=0;i<count;++i) {
        enum frame_role final=(enum frame_role)(FRAME_DATA+i%4);
        enum frame_role staged=final==FRAME_DATA?FRAME_STAGED_DATA:FRAME_STAGED_TABLE;
        need(frame_pool_allocate(&pool,staged,100+i,&ids[i])==FRAME_OK,"allocate real frame");
        volatile unsigned char *p=0;
        need(frame_pool_alias(&pool,ids[i],100+i,&p)==FRAME_OK && (uint64_t)p==ids[i].physical,"checked physical alias");
        for (unsigned j=0;j<4096;++j) { need(p[j]==0,"zero-before-use");p[j]=(unsigned char)(1+((i+j)%251)); }
        need(frame_pool_promote(&pool,ids[i],100+i,final)==FRAME_OK,"publish role");++expected[final];
    }
    struct frame_stats full;
    need(frame_pool_stats(&pool,&full)==FRAME_OK,"full stats");
    for (unsigned r=0;r<FRAME_ROLE_COUNT;++r) need(full.roles[r]==expected[r],"exact data/table counts");
    need(frame_pool_allocate(&pool,FRAME_STAGED_DATA,7,&result)==FRAME_EXHAUSTED && same_id(result,untouched),"bounded pool exhaustion");
    for (uint32_t i=0;i<count;++i) {
        volatile unsigned char *p=0;
        need(frame_pool_alias(&pool,ids[i],100+i,&p)==FRAME_OK,"owned pattern alias");
        for (unsigned j=0;j<4096;++j) need(p[j]==(unsigned char)(1+((i+j)%251)),"physical isolation");
    }
    need(frame_pool_cancel(&pool,ids[0],100)==FRAME_WRONG_ROLE,"live cancellation");
    need(frame_pool_retire(&pool,ids[0],999,FRAME_DATA)==FRAME_WRONG_OWNER,"wrong owner");
    need(frame_pool_retire(&pool,ids[0],100,FRAME_PT)==FRAME_WRONG_ROLE,"wrong role");
    need(frame_pool_cancel(&pool,(struct frame_id){(uint64_t)pml4,1},100)==FRAME_FOREIGN,"borrowed root free");
    need(frame_pool_cancel(&pool,(struct frame_id){original&~4095ull,1},100)==FRAME_FOREIGN,"reserved boot info free");
    need(frame_pool_cancel(&pool,(struct frame_id){0x100000,1},100)==FRAME_FOREIGN,"reserved kernel free");
    need(frame_pool_cancel(&pool,(struct frame_id){BOOTINFO_CEILING,1},100)==FRAME_FOREIGN,"foreign frame free");
    for (uint32_t i=0;i<count;++i)
        need(frame_pool_retire(&pool,ids[i],100+i,(enum frame_role)(FRAME_DATA+i%4))==FRAME_OK,"retire owned role");
    need(frame_pool_retire(&pool,ids[0],100,FRAME_DATA)==FRAME_WRONG_ROLE,"double retirement");
    volatile unsigned char *blocked=(volatile unsigned char *)0x123;
    need(frame_pool_alias(&pool,ids[0],100,&blocked)==FRAME_WRONG_ROLE && (uint64_t)blocked==0x123,"retiring inaccessible API");
    need(frame_pool_allocate(&pool,FRAME_STAGED_DATA,7,&result)==FRAME_EXHAUSTED,"retiring not reusable");
    uint32_t before=flushes,released=0;
    need(frame_pool_reclaim(&pool,&released)==FRAME_OK && released==count && flushes==before+1,"flush before reclaim");
    stats_free(count);
    need(frame_pool_cancel(&pool,ids[0],100)==FRAME_STALE,"double free");
    struct frame_id old=ids[0];
    need(frame_pool_allocate(&pool,FRAME_STAGED_DATA,77,&result)==FRAME_OK && result.physical==old.physical && result.generation!=old.generation,"physical reuse fresh identity");
    need(frame_pool_cancel(&pool,old,100)==FRAME_STALE,"stale token after reuse");
    volatile unsigned char *p=0;
    need(frame_pool_alias(&pool,result,77,&p)==FRAME_OK,"reuse alias");
    for (unsigned j=0;j<4096;++j) need(!p[j],"reuse zeros");
    need(frame_pool_cancel(&pool,result,77)==FRAME_OK,"cancel staging");
    stats_free(count);
    say("X64 FRAME PASS real zero isolation exhaustion release reuse roles\n");
    /* Each failure position in a real seven-allocation transaction. A caller
     * rolls its staged frames back, with no live ownership publication. */
    for (uint32_t n=1;n<=7;++n) {
        need(frame_pool_fail_after(&pool,n)==FRAME_OK,"arm allocation failure");
        uint32_t allocated=0;
        for (uint32_t j=0;j<7;++j) {
            result=untouched;
            error=frame_pool_allocate(&pool,j%2?FRAME_STAGED_DATA:FRAME_STAGED_TABLE,42,&result);
            if (j+1==n) { need(error==FRAME_INJECTED && same_id(result,untouched),"exact injected failure");break; }
            need(error==FRAME_OK,"allocation before injected failure");ids[allocated++]=result;
        }
        for (uint32_t j=0;j<allocated;++j) need(frame_pool_cancel(&pool,ids[j],42)==FRAME_OK,"staging rollback");
        stats_free(count);whole_map_audit();
    }
    need(frame_pool_fail_after(&pool,0)==FRAME_OK,"disable failure");
    need(frame_pool_allocate(&pool,FRAME_STAGED_TABLE,42,&result)==FRAME_OK,"post failure success");
    need(frame_pool_cancel(&pool,result,42)==FRAME_OK,"post failure cleanup");
#if FRAME_TEST_INJECT == 2
    ++pool.roles[FRAME_FREE];
#elif FRAME_TEST_INJECT == 3
    pt[pool.selection.frames[0]/4096]&=~FRAME_POOL_NX;
#elif FRAME_TEST_INJECT == 4
    pt[pool.selection.frames[0]/4096]|=4;
#endif
    stats_free(count);whole_map_audit();
    say("X64 FRAME PASS failures exact accounting preserved boot mappings\n");
    say("X64 FRAME POOL PASS BSP-only managed=");hex(count);say("\n");
#if VM_TEST
    pool.service_root=pml4;
    need(vm_init(&vm,&pool,alternate_root)==VM_STATE && !vm.ready && !pool.service_owner,"VM root identity");
    need(vm_init(&vm,&pool,pml4)==VM_OK,"VM initialization");
    vm_guest_tests(&vm);
    stats_free(count);whole_map_audit();
#endif
}

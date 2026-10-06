#include "frame_platform.h"
#if VM_TEST
#include "sparse_vm.h"
static struct frame_pool *bound_pool;
static struct vm_space *bound_vm;
#endif
extern uint64_t pml4[],pdpt[],pd[],pt[];
extern unsigned char stack_bottom[],stack_top[];
static uint32_t flushes;

static uint64_t msr(uint32_t n) { uint32_t a,d;__asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(n));return a|((uint64_t)d<<32); }
static int context(void *opaque) {
    (void)opaque;
    uint64_t c0,c3,c4,flags,sp;uint16_t cs;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2; pushfq; pop %3; mov %%rsp,%4; mov %%cs,%5"
        :"=r"(c0),"=r"(c3),"=r"(c4),"=r"(flags),"=r"(sp),"=r"(cs));
    if (cs!=24 || flags&(1ull<<9) || sp<(uint64_t)stack_bottom || sp>=(uint64_t)stack_top ||
        (c0&0x8001000d)!=0x8001000d || c3!=(uint64_t)pml4 || c4!=0x20 ||
        (msr(0xc0000080)&0xd00)!=0xd00 || !(msr(0x1b)&0x100)) return 0;
#if VM_TEST
    /* A published binding is already initialized; no pre-ready state exists. */
    if (bound_vm && (bound_pool->ready!=1 || bound_pool->service_root!=pml4 || bound_vm->ready!=1 ||
        bound_vm->pool!=bound_pool || bound_vm->root!=pml4 ||
        bound_pool->service_owner!=bound_vm)) return 0;
#endif
    /* Every borrowed intermediate is still the exact boot-owned supervisor tree. */
    if ((pml4[0]&~0x20ull)!=((uint64_t)pdpt|3) ||
        (pdpt[0]&~0x20ull)!=((uint64_t)pd|3)) return 0;
    for (unsigned i=0;i<512;++i) {
        if (i && pdpt[i]) return 0;
#if VM_TEST
        if (i && !bound_vm && pml4[i]) return 0;
#else
        if (i && pml4[i]) return 0;
#endif
        if ((pd[i]&~0x20ull)!=(i<32 ? (uint64_t)&pt[i*512]|3 : 0)) return 0;
    }
#if VM_TEST
    if (bound_vm && !vm_owned_hierarchy_valid(bound_vm)) return 0;
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
const struct frame_platform x64_frame_platform={context,leaf,write_leaf,flush,alias,0};

volatile uint64_t *x64_frame_boot_root(void) { return pml4; }
uint32_t x64_frame_flush_count(void) { return flushes; }

#if VM_TEST
enum vm_error x64_frame_vm_init(struct frame_pool *p,struct vm_space *s) {
    if (!p || !s) return VM_ARGUMENT;
    if (bound_pool || bound_vm || p->ready!=1 || p->busy || s->ready || s->busy ||
        p->service_owner || p->service_root) return VM_STATE;
    /* Only the immutable real backend can acquire this platform binding. */
    if (p->platform.context_ok!=context || p->platform.read_leaf!=leaf ||
        p->platform.write_leaf!=write_leaf || p->platform.flush!=flush ||
        p->platform.alias!=alias || p->platform.opaque) return VM_STATE;
    if (!context(0)) return VM_STATE;
    /* The only temporary publication is the core's required root. vm_init
     * validates the pool and makes no ownership writes on any failure path. */
    p->service_root=pml4;
    enum vm_error e=vm_init(s,p,pml4);
    if (e) { p->service_root=0;return e; }
    bound_pool=p;bound_vm=s;
    return VM_OK;
}
#endif

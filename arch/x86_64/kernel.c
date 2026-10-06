#include "handoff.h"
extern void frame_guest_tests(const void *, size_t, uint64_t);
#define NX (1ull<<63)
#define PAGE 4096ull
extern unsigned char __kernel_start[],__kernel_end[],__text_start[],__text_end[];
extern unsigned char __rodata_start[],__rodata_end[],__data_start[];
extern unsigned char stack_guard[],stack_bottom[],stack_top[];
extern unsigned char fault_guard[],fault_bottom[],fault_top[];
extern unsigned char df_guard[],df_bottom[],df_top[],nmi_guard[],nmi_top[],mc_guard[],mc_top[];
extern uint64_t pml4[],pdpt[],pd[],pt[],gdt[];
extern uint32_t boot_magic,boot_info,boot_size;
extern void *isr_table[];
extern void early_exception64(void);
extern void probe_write(void *),probe_read(void *),probe_execute(void *),probe_stack(void),probe_ud(void);
extern unsigned char probe_write_ip[],probe_write_resume[],probe_read_ip[],probe_read_resume[];
extern unsigned char probe_execute_resume[],probe_stack_ip[],probe_stack_resume[],probe_ud_ip[],probe_ud_resume[];
struct __attribute__((packed)) gate { uint16_t lo,selector; uint8_t ist,flags; uint16_t mid; uint32_t hi,reserved; };
extern struct gate idt64[256];
struct __attribute__((packed)) tss64 {
    uint32_t reserved0; uint64_t rsp[3]; uint64_t reserved1;
    uint64_t ist[7]; uint64_t reserved2; uint16_t reserved3,iomap;
};
_Static_assert(sizeof(struct tss64)==104,"TSS size");
static struct tss64 tss;
static unsigned char boot_copy[BOOTINFO_LIMIT] __attribute__((aligned(4096)));
static unsigned char nx_code[4096] __attribute__((aligned(4096))) = {0xc3};
static const unsigned char ro_target[4096] __attribute__((aligned(4096))) = {0x5a};
static inline void outb(uint16_t p,uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static void puts64(const char *s) { while (*s) outb(0xe9,(unsigned char)*s++); }
static void hex(uint64_t n) { for (int i=60;i>=0;i-=4) outb(0xe9,"0123456789abcdef"[(n>>i)&15]); }
__attribute__((noreturn)) static void stop(int ok) {
    __asm__ volatile("outl %0,%1"::"a"(ok?0x10u:0x11u),"Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("cli; hlt");
}
__attribute__((noreturn)) static void fail(const char *why) {
    puts64("X64 FAIL "); puts64(why); puts64("\n"); stop(0);
}
static uint64_t cr0(void) { uint64_t x; __asm__ volatile("mov %%cr0,%0":"=r"(x)); return x; }
static uint64_t cr3(void) { uint64_t x; __asm__ volatile("mov %%cr3,%0":"=r"(x)); return x; }
static uint64_t cr4(void) { uint64_t x; __asm__ volatile("mov %%cr4,%0":"=r"(x)); return x; }
static uint64_t msr(uint32_t n) { uint32_t a,d; __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(n)); return a|((uint64_t)d<<32); }
static void flush(void) { __asm__ volatile("mov %0,%%cr3"::"r"(pml4):"memory"); }
static void descriptors(void) {
    tss.rsp[0]=(uint64_t)stack_top;
    tss.ist[0]=(uint64_t)fault_top;
    tss.ist[1]=(uint64_t)df_top;
    tss.ist[2]=(uint64_t)nmi_top;
    tss.ist[3]=(uint64_t)mc_top;
    tss.iomap=sizeof(tss); /* No I/O bitmap within descriptor limit. */
    uint64_t a=(uint64_t)&tss;
    gdt[4]=(sizeof(tss)-1) | ((a&0xffffff)<<16) | (0x89ull<<40) | (((a>>24)&255)<<56);
    gdt[5]=a>>32;
    __asm__ volatile("ltr %w0"::"r"((uint16_t)32):"memory");
    for (unsigned i=0;i<256;++i) {
        a=(uint64_t)(i<32 ? isr_table[i] : early_exception64);
        idt64[i]=(struct gate){a,24,1,0x8e,a>>16,a>>32,0};
    }
    idt64[8].ist=2; idt64[2].ist=3; idt64[18].ist=4;
#if TEST_INJECT == 6
    idt64[14].flags=0x0e; /* Non-present #PF gate forces a real #DF. */
#endif
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } ptr={sizeof(struct gate)*256-1,(uint64_t)idt64};
    __asm__ volatile("lidt %0"::"m"(ptr):"memory");
    /* Descriptor pages become read-only after CPU has marked TSS busy. */
    pt[(uint64_t)gdt/PAGE]&=~2ull;
    pt[(uint64_t)idt64/PAGE]&=~2ull;
    flush();
    outb(0x70,0); /* PC legacy NMI enabled again, RTC index 0 selected. */
}
struct frame { uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rbx,rdx,rcx,rax;
               uint64_t vector,error,rip,cs,flags,rsp,ss; };
_Static_assert(offsetof(struct frame,vector)==120,"trap assembly layout");
static volatile struct {
    uint64_t armed,vector,error,address,rip,resume,stack;
} expected;
static volatile unsigned faults;
void handle_trap(struct frame *f) {
    uint64_t address; __asm__ volatile("mov %%cr2,%0":"=r"(address));
    int good = expected.armed==1 && f->vector==expected.vector && f->error==expected.error &&
        f->rip==expected.rip && f->cs==24 && f->ss==16 && !(f->flags&(1ull<<9)) &&
        (uint64_t)f >= (uint64_t)fault_bottom && (uint64_t)(f+1)<=(uint64_t)fault_top &&
        (f->vector!=14 || address==expected.address) &&
        (expected.stack ? f->rsp==expected.stack :
            (f->rsp>=(uint64_t)stack_bottom && f->rsp<(uint64_t)stack_top));
    expected.armed=0; /* An unexpected/nested fault can never satisfy this probe. */
    if (!good) {
        puts64("X64 TRAP vector=");hex(f->vector);puts64(" error=");hex(f->error);
        puts64(" rip=");hex(f->rip);puts64(" cr2=");hex(address);puts64("\n");
        if (f->vector==8 && f->error==0 && (uint64_t)f>=(uint64_t)df_bottom &&
            (uint64_t)(f+1)<=(uint64_t)df_top) puts64("X64 DOUBLE FAULT on dedicated IST\n");
        fail("unexpected exception");
    }
    f->rip=expected.resume;
    ++faults;
    puts64("X64 EXPECTED vector=");hex(f->vector);puts64(" error=");hex(f->error);
    puts64(" rip=");hex(expected.rip);puts64(" address=");hex(expected.address);puts64("\n");
}
static void arm(uint64_t vector,uint64_t error,uint64_t addr,void *rip,void *resume,uint64_t stack) {
    if (expected.armed) fail("probe still armed");
    expected.vector=vector;expected.error=error;expected.address=addr;
    expected.rip=(uint64_t)rip;expected.resume=(uint64_t)resume;expected.stack=stack;
    __asm__ volatile("":::"memory"); expected.armed=1;
}
static void complete(const char *name,unsigned count) {
    if (expected.armed || faults!=count) fail("fault probe did not trap");
    puts64("X64 PASS ");puts64(name);puts64("\n");
}
static int guard(uint64_t a) {
    return a==(uint64_t)stack_guard || a==(uint64_t)fault_guard || a==(uint64_t)df_guard ||
           a==(uint64_t)nmi_guard || a==(uint64_t)mc_guard;
}
static void mappings(void) {
    /* Inspect every hierarchy entry and leaf; hardware A/D bits are allowed. */
    if ((pml4[0]&~0x20ull)!=((uint64_t)pdpt|3) || (pdpt[0]&~0x20ull)!=((uint64_t)pd|3)) fail("paging root");
    for (unsigned i=1;i<512;++i) if (pml4[i] || pdpt[i]) fail("unexpected upper mapping");
    for (unsigned i=0;i<512;++i) {
        uint64_t want=i<32 ? (uint64_t)&pt[i*512]|3 : 0;
        if ((pd[i]&~0x20ull)!=want) fail("paging directory");
    }
    unsigned present=0;
    for (uint64_t a=0;a<BOOTINFO_CEILING;a+=PAGE) {
        uint64_t want=0;
        if (a>=(uint64_t)__text_start && a<(uint64_t)__text_end) want=a|1;
        else if (a>=(uint64_t)__rodata_start && a<(uint64_t)__rodata_end) want=a|1|NX;
        else if (a>=(uint64_t)__data_start && a<(uint64_t)__kernel_end && !guard(a)) want=a|3|NX;
        if (a==(uint64_t)gdt || a==(uint64_t)idt64) want&=~2ull;
        if ((pt[a/PAGE]&~0x60ull)!=want) fail("unexpected leaf permissions");
        if (want) ++present;
    }
    puts64("X64 PASS sparse W^X supervisor mappings pages=");hex(present);puts64("\n");
}
void kernel_main(void) {
    descriptors();
    uint16_t cs; uint64_t flags;
    __asm__ volatile("mov %%cs,%0; pushfq; pop %1":"=r"(cs),"=r"(flags));
    if (cs!=24 || (flags&(1ull<<9)) || (cr0()&0x8001000d)!=0x8001000d ||
        cr3()!=(uint64_t)pml4 || cr4()!=0x20 || (msr(0xc0000080)&0xd00)!=0xd00) fail("long mode controls");
    puts64("X64 PASS long mode CR0=");hex(cr0());puts64(" CR4=");hex(cr4());puts64(" EFER=");hex(msr(0xc0000080));puts64("\n");
    for (uint32_t i=0;i<boot_size;++i) boot_copy[i]=((volatile unsigned char *)(uint64_t)boot_info)[i];
    /* Original loader buffer is never retained. Revoke it before parsing the copy. */
    uint64_t first=(uint64_t)boot_info&~(PAGE-1),end=((uint64_t)boot_info+boot_size+PAGE-1)&~(PAGE-1);
    for (uint64_t a=first;a<end;a+=PAGE) pt[a/PAGE]=0;
    flush();
#if TEST_INJECT == 3
    boot_copy[12]=0;boot_copy[13]=0;boot_copy[14]=0;boot_copy[15]=0;
#elif TEST_INJECT == 4
    boot_copy[boot_size-4]=16;
#endif
    struct handoff_summary info;
    const char *error=handoff_validate(boot_copy,boot_size,(uint64_t)__kernel_start,(uint64_t)__kernel_end,&info);
    if (error) fail(error);
    puts64("X64 PASS bounded handoff copy bytes=");hex(boot_size);puts64(" mmap entries=");hex(info.mmap_entries);puts64(" usable=");hex(info.usable_bytes);puts64("\n");
    mappings();
    arm(14,3,(uint64_t)probe_write_ip,probe_write_ip,probe_write_resume,0);
#if TEST_INJECT == 14
    expected.vector=13;
#elif TEST_INJECT == 15
    expected.error=1;
#elif TEST_INJECT == 16
    ++expected.address;
#elif TEST_INJECT == 17
    ++expected.rip;
#elif TEST_INJECT == 18
    expected.resume=(uint64_t)probe_ud_ip;
#elif TEST_INJECT == 19
    expected.stack=1;
#endif
    probe_write(probe_write_ip);complete("WP text write",1);
    arm(14,3,(uint64_t)ro_target,probe_write_ip,probe_write_resume,0);
    probe_write((void *)ro_target);complete("WP rodata write",2);
    arm(14,17,(uint64_t)nx_code,nx_code,probe_execute_resume,0);
    probe_execute(nx_code);complete("NX data execution",3);
    arm(14,0,0,probe_read_ip,probe_read_resume,0);
    probe_read(0);complete("null read",4);
    arm(14,2,(uint64_t)stack_bottom-8,probe_stack_ip,probe_stack_resume,(uint64_t)stack_bottom);
    probe_stack();complete("stack overflow on IST",5);
    arm(14,0,boot_info,probe_read_ip,probe_read_resume,0);
    probe_read((void *)(uint64_t)boot_info);complete("boot-info mapping revoked",6);
    arm(6,0,0,probe_ud_ip,probe_ud_resume,0);
    probe_ud();complete("invalid opcode containment",7);
#if TEST_INJECT == 5
    probe_ud(); /* Unarmed exception must fail closed. */
#endif
    puts64("X64 FOUNDATION PASS BSP-only tests=7\n");
    frame_guest_tests(boot_copy,boot_size,boot_info);
    stop(1);
}

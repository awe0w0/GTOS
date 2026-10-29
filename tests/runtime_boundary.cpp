// GTOS-DSH-118: the boundary the NativeRuntime host unit stands on, plus the in-process fault observer.
//
// The 115 review named the gap: PrepareStacks and Activate had only a guest positive initialisation, with no unit of
// their own, so their resource failure paths and their guard and ownership obligations were never measured. This file
// supplies what a host cannot supply and decides nothing:
//
//   the bootstrap-processor identity    a CPUID and an APIC-base MSR read on a real machine
//   CR3                                 a control-register read
//   fast-entry disabling                an MSR write
//   interrupt masking                   CLI/STI, already seamed by the existing GTOS_MEMORY_TEST macro
//   the memory the firmware describes   an addressable-frame count and a free range
//
// Everything the runtime and the allocator decide is the production code: the frame bitmap, the free count, the page
// table construction, the stack layout, the guard windows, the rollback on failure and the state the object publishes.
// A test that installed a stack or marked a page mapped here would be writing the answer it is supposed to measure.

#include <common/types.h>
#include <memory/physical.h>
#include <memory/paging.h>
#include <process/native_runtime.h>
#include <memory/bootstrap.h>
#include <hardwarecommunication/port.h>

using namespace gtos::memory;

// The linker symbols a real image provides. Placed in ordered sections so the read-only range is never empty, which is the
// same guarantee the production linker script gives.
// The memory the machine has. Placed at a fixed low address by the unit's linker script, so a frame number the allocator
// returns is also the address of the frame: that is what an identity map gives the kernel, and it is what makes the
// production allocator and page-table code work unmodified here.
extern "C" {
__attribute__((section(".gtos_arena"), aligned(4096))) uint8_t gtos_host_arena[5 * 1024 * 1024] = {0};
__attribute__((section(".gtos_kernel"))) uint8_t kernel_start[1] = {0};
__attribute__((section(".gtos_kernel"))) uint8_t kernel_end[1] = {0};
__attribute__((section(".gtos_ro_start"))) uint8_t kernel_readonly_start[1] = {0};
__attribute__((section(".gtos_ro_end"))) uint8_t kernel_readonly_end[1] = {0};
}


// ---------------------------------------------------------------- the fault observer
// A host cannot read a fault's PC or address from outside without a debugger, and none exists here. The process can read
// its own. This installs a SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT handler on a separate stack, records the signal, si_addr
// and EIP, prints them, and exits with 70+signal. It never returns to the faulting instruction, so it cannot turn a fault
// into a pass; the runner sees a distinctive status instead of an unexplained -11.
namespace {

struct KernelSigaction {
    void* handler;
    unsigned long flags;
    void* restorer;
    unsigned long mask[2];
};
struct KernelStack {
    void* sp;
    int flags;
    unsigned int size;
};
const int kSaSigInfo = 0x00000004;
const int kSaOnStack = 0x08000000;
const int kSaNoDefer = 0x40000000;

char faultText[512];
unsigned faultTextBytes = 0;
void Append(char c) { if (faultTextBytes < sizeof(faultText)) faultText[faultTextBytes++] = c; }
void Append(const char* s) { while (*s) Append(*s++); }
void AppendHex(unsigned v) {
    Append('0'); Append('x');
    for (int shift = 28; shift >= 0; shift -= 4) Append("0123456789ABCDEF"[(v >> shift) & 0xF]);
}
int WriteStderr(const char* text, unsigned bytes) {
    int result;
    asm volatile("int $0x80" : "=a"(result) : "a"(4), "b"(2), "c"(text), "d"(bytes) : "memory", "cc");
    return result;
}

// si_addr is at offset 12 of siginfo_t on i386; EIP is gregs[14] of sigcontext, which sits 20 bytes into ucontext_t.
extern "C" void GtosFaultHandler(int signal, void* info, void* context) {
    const unsigned* words = (const unsigned*)info;
    const unsigned address = words[3];
    // ucontext_t: uc_flags, uc_link, uc_stack{sp,flags,size} = 20 bytes, then sigcontext.
    // sigcontext: gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trapno,err,eip,cs,eflags,esp_at_signal,ss
    const unsigned* frame = (const unsigned*)context;
    const unsigned eip = frame[19];
    const unsigned esp = frame[12];
    const unsigned ebp = frame[11];
    const unsigned eax = frame[16];
    Append("FAULT sig="); AppendHex((unsigned)signal);
    Append(" addr="); AppendHex(address);
    Append(" eip="); AppendHex(eip);
    Append(" esp="); AppendHex(esp);
    Append(" ebp="); AppendHex(ebp);
    Append(" eax="); AppendHex(eax);
    Append('\n');
    WriteStderr(faultText, faultTextBytes);
    // Candidate return addresses: stack words that point into this image. The image is linked at 0x00100000, so anything in
    // that range is a code address this process would have returned to.
    faultTextBytes = 0;
    Append("STACK");
    const unsigned* stack = (const unsigned*)esp;
    for (int i = 0; i < 96; ++i) {
        const unsigned value = stack[i];
        if (value >= 0x00100000u && value < 0x00120000u) {
            Append(' '); AppendHex(value); Append('@'); AppendHex((unsigned)(i * 4));
        }
    }
    Append('\n');
    WriteStderr(faultText, faultTextBytes);
    int result;
    asm volatile("int $0x80" : "=a"(result) : "a"(252), "b"(70 + signal) : "memory", "cc");
    for (;;) asm volatile("hlt" : : : "memory");
}

__attribute__((aligned(16))) char faultStack[32768];

} // namespace

// Install the observer. Returns whether every signal was accepted; a refusal is reported, never ignored.
extern "C" int GtosInstallFaultObserver() {
    KernelStack stack;
    stack.sp = faultStack;
    stack.flags = 0;
    stack.size = sizeof(faultStack);
    int result;
    asm volatile("int $0x80" : "=a"(result) : "a"(186), "b"(&stack) : "memory", "cc");
    const int stackOk = result == 0;
    KernelSigaction action;
    action.handler = (void*)&GtosFaultHandler;
    action.flags = kSaSigInfo | kSaOnStack | kSaNoDefer;
    action.restorer = 0;
    action.mask[0] = 0; action.mask[1] = 0;
    const int signals[5] = { 11, 7, 4, 8, 6 };
    int installed = 0;
    for (int i = 0; i < 5; ++i) {
        int rc;
        asm volatile("int $0x80" : "=a"(rc) : "a"(174), "b"(signals[i]), "c"(&action), "d"(0), "S"(8)
                     : "memory", "cc");
        if (rc == 0) ++installed;
    }
    return (stackOk ? 1 : 0) | (installed << 8);
}

// ---------------------------------------------------------------- the machine's answers
extern "C" {
// Whether this processor is the bootstrap one, whether CR3 currently names the shared directory, and whether the
// fast-entry MSRs could be cleared. All three are answers, not decisions.
uint32_t gtos_host_runtime_bsp = 1;
uint32_t gtos_host_runtime_cr3_value = 0;
uint32_t gtos_host_runtime_fast_entry_ok = 1;
// The flag register the runtime sees. Clear, because the guest runs with interrupts off; a host process does not, so this
// has to be answered rather than read.
uint32_t gtos_host_runtime_flags_value = 0x00000002U;
// The CR0 value the runtime records at activation. Paging and write-protect set, as the guest has them.
uint32_t gtos_host_runtime_cr0_value = 0x80010011U;

// Counters, so a test can show the runtime really asked.
uint32_t gtos_host_runtime_cr3_reads = 0;
uint32_t gtos_host_runtime_fast_entry_calls = 0;
}

// The four registers the process address space would read, and the invalidation it would perform. The unit sets the CR3 value
// to the directory the paging object froze before it creates anything, so the production "am I in the right context" check
// is answered truthfully rather than bypassed.
extern "C" uint32_t gtos_process_memory_test_cr0 = 0x80010011U;
extern "C" uint32_t gtos_process_memory_test_cr3 = 0;
extern "C" uint32_t gtos_process_memory_test_cr4 = 0;
extern "C" bool gtos_process_memory_test_bsp = true;
// The declared host-only fault point for the late stage of Create. It is defined here because this file is the host
// boundary: the one place allowed to answer what a host cannot do. Zero is the production path.
extern "C" uint32_t gtos_process_memory_test_fail_protect_after = 0;
// The seam's witness: how often the stage was consulted, how often it refused, and the page count it saw when it did.
extern "C" uint32_t gtos_process_memory_test_protect_calls = 0;
extern "C" uint32_t gtos_process_memory_test_protect_refusals = 0;
extern "C" uint32_t gtos_process_memory_test_protect_pages_at_refusal = 0;
extern "C" void gtos_process_memory_test_invalidate(uint32_t address) { (void)address; }
extern "C" uint32_t gtos_host_runtime_is_bsp() { return gtos_host_runtime_bsp; }
extern "C" uint32_t gtos_host_runtime_cr3() {
    ++gtos_host_runtime_cr3_reads;
    return gtos_host_runtime_cr3_value;
}
extern "C" uint32_t gtos_host_runtime_fast_entry() {
    ++gtos_host_runtime_fast_entry_calls;
    return gtos_host_runtime_fast_entry_ok;
}
extern "C" uint32_t gtos_host_runtime_flags() { return gtos_host_runtime_flags_value; }
extern "C" uint32_t gtos_host_runtime_cr0() { return gtos_host_runtime_cr0_value; }

// ---------------------------------------------------------------- what the unit must never reach
//
// The production enable() loads CR3 and sets CR0, which a host cannot do. It is left exactly as production wrote it: the
// unit never enables paging, because Activate's own preconditions are what is under test and they are checked before any
// control register is touched. If a test ever reached enable() the host would fault, which is the honest signal that the
// test took a path it was not written for -- rather than a stub quietly reporting success.

// ---------------------------------------------------------------- layers this unit does not exercise
//
// The console writers and the boot-only SIPI pool belong to layers a host unit deliberately does not reach. They exist
// here so the linker is satisfied. Anything that would be a lie if a unit reached it halts, so an accidental dependency
// fails loudly rather than passing on fabricated behaviour.
// native_runtime.cpp declares these two at file scope with C++ linkage, so they are defined the same way here.
void printf(char* text) { (void)text; for (;;) asm volatile("cli; hlt" : : : "memory"); }
void printfHex32(uint32_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }
// A pure virtual call reaches a table slot that has no definition. The kernel image answers that with its own handler; this image
// is linked separately from the kernel, and the compiler only emits the abstract tables that name this symbol at -O0, so the host
// side owes the same contract at every optimization level. It does not return, because returning would resume in the slot that
// does not exist; it says what happened on the descriptor the unit already uses and ends the process with its own status, which
// a runner can tell from a pass and from a fault.
extern "C" void __cxa_pure_virtual() {
    static const char text[] = "runtime unit: pure virtual call\n";
    WriteStderr(text, (unsigned)(sizeof(text) - 1));
    for (;;) asm volatile("int $0x80" : : "a"(1), "b"(97) : "memory");
}

// The port layer executes IN/OUT, which a host cannot do. Activate refuses before it ever samples the realtime clock, so
// these are never reached; reaching one halts rather than answering, which is the honest signal.
namespace gtos { namespace hardwarecommunication {

Port::Port(uint16_t port) : portnumber(port) {}
Port::~Port() {}
Port8Bit::Port8Bit(uint16_t port) : Port(port) {}
Port8Bit::~Port8Bit() {}
uint8_t Port8Bit::Read() { for (;;) asm volatile("cli; hlt" : : : "memory"); }
void Port8Bit::Write(uint8_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }

} }

namespace gtos { namespace memory {

LowBootstrapPool::LowBootstrapPool() {}
void LowBootstrapPool::clear() {}
void LowBootstrapPool::addAvailable(uint64_t, uint64_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }
// The pool's domain is the SIPI pages below 1 MiB. reserveRegion is a production call this unit does make, and the range it
// reserves is the kernel image at 2 MiB, which the pool would legitimately ignore. A no-op is therefore the true answer;
// halting here would fault on a call the unit is supposed to make.
void LowBootstrapPool::reserve(uint64_t address, uint64_t length) { (void)address; (void)length; }
bool LowBootstrapPool::restrictFirmware(uint32_t, uint32_t, uint32_t) { return false; }
bool LowBootstrapPool::claim(uint32_t&) { return false; }
bool LowBootstrapPool::intersectsClaimed(uint64_t, uint64_t) const { return false; }
bool LowBootstrapPool::isClaimed(uint32_t) const { return false; }
uint32_t LowBootstrapPool::freePages() const { return 0; }
uint32_t LowBootstrapPool::claimedPages() const { return 0; }
void LowBootstrapPool::range(uint64_t, uint64_t, uint32_t& first, uint32_t& last) const {
    first = 0;
    last = 0;
}

} }

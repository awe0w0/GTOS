#include <process/native_fp.h>
#include <memory/paging.h>
using namespace gtos::process;
void printf(char*);
namespace {
    uint32_t Cr0() { uint32_t n; asm volatile("mov %%cr0,%0" : "=r"(n)); return n; }
    uint32_t Cr4() { uint32_t n; asm volatile("mov %%cr4,%0" : "=r"(n)); return n; }
    bool InterruptsOff() { uint32_t n; asm volatile("pushfl; popl %0" : "=r"(n)); return !(n & 0x200); }
    bool Resident(gtos::memory::KernelPaging& kernel, const void* data, uint32_t bytes) {
        const uint32_t address = (uint32_t)data;
        if (!address || !bytes || (uint64_t)address + bytes > 0x100000000ULL) return false;
        for (uint64_t page = address & ~4095U; page < (uint64_t)address + bytes; page += 4096) {
            gtos::memory::PagingMapping mapping;
            if (!kernel.query((uint32_t)page, mapping) || !mapping.writable
                || mapping.userAccessible || mapping.cacheDisabled) return false;
        }
        return true;
    }
    NativeFpCapabilities Capabilities() {
        NativeFpCapabilities c = {};
        uint32_t before, after;
        asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%1;"
                     "pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(before), "=&r"(after) : : "cc", "memory");
        c.cpuid = ((before ^ after) & 0x200000) != 0;
        if (!c.cpuid) return c;
        uint32_t a, b, d;
        asm volatile("cpuid" : "=a"(c.maximumLeaf), "=b"(b), "=c"(a), "=d"(d) : "a"(0), "c"(0));
        if (c.maximumLeaf >= 1)
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c.ecx), "=d"(c.edx) : "a"(1), "c"(0));
        c.cr4 = Cr4(); return c;
    }
    bool CanonicalObserved(const NativeFpContext& c) {
        if (NativeFpGet16(c.fx) != 0x37F || NativeFpGet16(c.fx + 2) || c.fx[4]
            || NativeFpGet16(c.fx + 6) || NativeFpGet32(c.fx + 8) || NativeFpGet16(c.fx + 12)
            || NativeFpGet32(c.fx + 16) || NativeFpGet16(c.fx + 20)
            || NativeFpGet32(c.fx + 24) != 0x1F80 || NativeFpGet16(c.env) != 0x37F
            || NativeFpGet16(c.env + 4) || NativeFpGet16(c.env + 8) != 0xFFFF
            || NativeFpGet32(c.env + 12) || NativeFpGet16(c.env + 16)
            || (NativeFpGet16(c.env + 18) & 0x7FF) || NativeFpGet32(c.env + 20)
            || NativeFpGet16(c.env + 24)) return false;
        for (uint32_t reg = 0; reg < 8; ++reg)
            for (uint32_t byte = 0; byte < 10; ++byte)
                if (c.fx[32 + reg * 16 + byte]) return false;
        for (uint32_t i = 160; i < 288; ++i) if (c.fx[i]) return false;
        return true;
    }
}
NativeFp::NativeFp() : count(0), mask(0), saves(0), initializations(0), invalidations(0),
    failures(0), userKeyboardInterrupts(0), userMouseInterrupts(0), policy(NativeFpDisabled), error(NativeFpOk) {
    transition.phase = NativeFpOff; transition.owner = 0;
    transition.generation = transition.restores = 0;
    NativeFpScrub(&canonical, sizeof(canonical)); NativeFpScrub(&scratch, sizeof(scratch));
    for (uint32_t i = 0; i < 4; ++i) records[i] = 0;
}
bool NativeFp::PrepareBsp(NativeFpPolicy requested, gtos::memory::KernelPaging& kernel,
                          NativeFpRecord* const* supplied, uint32_t suppliedCount) {
    if (transition.phase != NativeFpOff || !InterruptsOff()) { error = NativeFpBadContext; return false; }
    if (requested == NativeFpDisabled) return true; // Exact integer-only compatibility.
    error = ValidateNativeFpCapabilities(Capabilities(), requested);
    if (error != NativeFpOk) return false;
    if (!Resident(kernel, this, sizeof(*this)) || ((uint32_t)&canonical & 15U)
        || ((uint32_t)&scratch & 15U) || !supplied || suppliedCount != 4) {
        error = NativeFpBadBuffer; return false;
    }
    for (uint32_t i = 0; i < suppliedCount; ++i) {
        if (!supplied[i] || ((uint32_t)supplied[i] & 15U)
            || !Resident(kernel, supplied[i], sizeof(NativeFpRecord))) { error = NativeFpBadBuffer; return false; }
        const uint64_t begin = (uint32_t)supplied[i], end = begin + sizeof(NativeFpRecord);
        const uint64_t module = (uint32_t)this;
        if (begin < module + sizeof(*this) && module < end) { error = NativeFpBadBuffer; return false; }
        for (uint32_t j = 0; j < i; ++j) {
            const uint64_t other = (uint32_t)supplied[j];
            if (begin < other + sizeof(NativeFpRecord) && other < end) { error = NativeFpBadBuffer; return false; }
        }
    }
    // All rejectable policy/buffer conditions precede the first FP/control mutation.
    const uint32_t old0 = Cr0(), old4 = Cr4();
    const uint32_t desired0 = NativeFpKernelCr0(old0), desired4 = NativeFpCr4(old4);
    asm volatile("mov %0,%%cr4; mov %1,%%cr0" : : "r"(desired4), "r"(desired0) : "memory");
    if (Cr0() != desired0 || Cr4() != desired4) {
        error = NativeFpControlFailure; Panic("activation controls");
    }
    native_fp_probe_asm(&scratch);
    mask = NativeFpMxcsrMask(NativeFpGet32(scratch.fx + 28));
    NativeFpBuildCanonical(canonical, scratch.env, mask);
    if (!NativeFpValidImage(canonical, mask)) { error = NativeFpCanonicalFailure; Panic("initial image"); }
    native_fp_neutral_asm(&canonical);
    NativeFpScrub(&scratch, sizeof(scratch));
    // Capture the actual neutral image/environment before any process exists.
    asm volatile("clts" : : : "memory");
    native_fp_save_asm(&scratch);
    native_fp_neutral_asm(&canonical);
    if (!CanonicalObserved(scratch)) { error = NativeFpCanonicalFailure; Panic("canonical roundtrip"); }
    NativeFpScrub(&scratch, sizeof(scratch));
    for (uint32_t i = 0; i < suppliedCount; ++i) {
        records[i] = supplied[i]; NativeFpScrub(records[i], sizeof(*records[i]));
    }
    count = suppliedCount; policy = requested; transition.phase = NativeFpKernelNeutral;
    return true;
}
bool NativeFp::Registered(const NativeFpRecord* record) const {
    for (uint32_t i = 0; i < count; ++i) if (records[i] == record) return true;
    return false;
}
bool NativeFp::Controls(bool user) const {
    return InterruptsOff() && (Cr0() & 0x2EU) == (user ? 0x22U : 0x2AU)
        && (Cr4() & (0x600U | (1U << 18))) == 0x600U;
}
bool NativeFp::Initialize(NativeFpRecord& record, uint32_t generation) {
    if (!Enabled()) return true;
    if (!NativeFpCanInitialize(transition, record, generation) || !Controls(false)
        || !Registered(&record)) return false;
    NativeFpCopy(record.image, canonical); record.generation = generation; record.initialized = 1;
    ++initializations; return true;
}
void NativeFp::Invalidate(NativeFpRecord& record) {
    if (!Enabled()) return;
    if (!NativeFpNeutral(transition) || !Controls(false) || !Registered(&record)) Panic("invalidate owner");
    NativeFpScrub(&record, sizeof(record)); ++invalidations;
}
void NativeFp::EnterKernel(NativeFpRecord* record, uint32_t generation, bool user, uint32_t vector) {
    if (!Enabled()) return;
    if (!user) {
        if (!NativeFpNeutral(transition) || !Controls(false)) Panic("nested transition/kernel owner");
        return;
    }
    if (vector == 7) Panic("unexpected user #NM");
    if (!NativeFpCanEnter(transition, record, generation) || !Controls(true) || !Registered(record))
        Panic("user entry owner");
    transition.phase = NativeFpSavingUser;
    native_fp_save_asm(&record->image);
    NativeFpPatchPointers(record->image);
    native_fp_neutral_asm(&canonical);
    ++saves;
    if (vector == 0x21) ++userKeyboardInterrupts;
    if (vector == 0x2C) ++userMouseInterrupts;
    transition.owner = 0; transition.generation = 0;
    transition.phase = NativeFpKernelNeutral;
}
NativeFpTransition* NativeFp::PrepareReturn(NativeFpRecord* record, uint32_t generation, bool user) {
    if (!Enabled()) return 0;
    if (!NativeFpNeutral(transition) || !Controls(false)) Panic("return phase");
    if (!user) return 0;
    if (!Registered(record) || !NativeFpCanReturn(transition, record, generation)
        || !NativeFpValidImage(record->image, mask)) Panic("return image/owner");
    transition.owner = record; transition.generation = generation;
    transition.phase = NativeFpRestoringUser;
    return &transition;
}
NativeFpStatistics NativeFp::Statistics() const {
    NativeFpStatistics result = {saves, transition.restores, initializations, invalidations, failures,
        userKeyboardInterrupts, userMouseInterrupts}; return result;
}
void NativeFp::Panic(const char* why) {
    asm volatile("cli" : : : "memory");
    transition.phase = NativeFpFailed; ++failures;
    printf((char*)"\nPANIC NATIVE FP invariant: "); printf((char*)why); printf((char*)"\n");
    for (;;) asm volatile("cli; hlt");
}

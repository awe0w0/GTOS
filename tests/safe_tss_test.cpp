// GTOS-DSH-138 H: the TSS is read only after every precondition holds, and each refusal names itself.
//
// The observation code under test is production code. What a host cannot do is execute STR or SGDT, or read a descriptor
// table without faulting; those are supplied through the seam in tests/gdt_host_seam.cpp, exactly as the runtime already
// takes CR0 and CR3 from its own boundary.
//
// 137's version of this unit had three gaps, and this one closes them:
//
//   1. It built its "runtime table" by hand-encoding a 0x89 descriptor into a local array. A hand-built table proves
//      something about a hand-built table. This unit constructs a real GlobalDescriptorTable, calls the production
//      LoadTaskState with a real stack top, and states its expectations from the runtime's own initialisation state
//      (TaskStateBase and friends). The descriptor is compared against the object it was built from, and esp0 is checked
//      against the stack this unit actually loaded.
//
//   2. Every refusal only asserted `bad > 0` and printed `why[0]`. A refusal that fired for the wrong reason looked
//      identical to one that fired for the right one, and a case whose earlier gate tripped first was indistinguishable
//      from a case that reached its own gate. Every case here asserts the exact first refusal string, so a single broken
//      condition cannot hide behind a different refusal.
//
//   3. The read counters were one window-wide total. They are per region now, charged before the window test, so a refusal
//      that must not touch the task state can be shown to have charged no task-state byte even when the address it refused
//      was a legal one inside the window.

#include <common/types.h>
#include <gdt.h>
#include "gdt_host_seam.h"

using namespace gtos;
using namespace gtos::testseam;

static uint32_t failures = 0;

// The unit constructs a GlobalDescriptorTable in raw storage, so it supplies the one placement-new form it uses. A
// replacement allocation function has to be at global scope; freestanding code has no <new> to take it from.
void* operator new(unsigned int, void* where) { return where; }
void operator delete(void*, void*) {}

static uint32_t Length(const char* text) { uint32_t n = 0; while (text[n]) ++n; return n; }
static int WriteAll(const char* text, uint32_t length) {
    int result = 0;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(4), "b"(2), "c"(text), "d"(length) : "memory");
    return result;
}
static void Say(const char* text) { WriteAll(text, Length(text)); }
static void SayHex(uint32_t value) {
    char digits[] = "0123456789ABCDEF";
    char text[9];
    for (int i = 7; i >= 0; --i) { text[i] = digits[value & 0xF]; value >>= 4; }
    text[8] = 0;
    Say(text);
}
static void Expect(bool condition, const char* label) {
    if (condition) return;
    ++failures;
    Say("  FAIL ");
    Say(label);
    Say("\n");
}

// Text equality, so a refusal can be asserted by name rather than printed and hoped over.
static bool SameText(const char* a, const char* b) {
    if (a == 0 || b == 0) return a == b;
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return false; ++i; }
    return a[i] == b[i];
}

// The stack the runtime is given. The TSS's esp0 must be this number, and the observation must read it back.
static uint8_t kernelStack[4096];

// A real GDT, built by the production constructor, with the production LoadTaskState.
static uint8_t gdtStorage[sizeof(GlobalDescriptorTable)] __attribute__((aligned(16)));

static GlobalDescriptorTable& Descriptors() { return *(GlobalDescriptorTable*)gdtStorage; }

// The descriptor bytes, addressed the way the observation addresses them: an index into the table the GDT register points
// at. Mutating a byte here is mutating the real table, not a copy of it.
static uint8_t* DescriptorAt(uint32_t index) {
    return (uint8_t*)Descriptors().DescriptorTableBase() + index * 8;
}

// The declared readable window: the GDT and the task state, which are two separate objects, so the window is their union.
static void DeclareWindow() {
    const uint32_t tableStart = Descriptors().DescriptorTableBase();
    const uint32_t tableEnd = tableStart + 8 * 8;
    const uint32_t tssStart = Descriptors().TaskStateBase();
    const uint32_t tssEnd = tssStart + (Descriptors().TaskStateLimit() + 1);
    const uint32_t base = tableStart < tssStart ? tableStart : tssStart;
    const uint32_t end = tableEnd > tssEnd ? tableEnd : tssEnd;
    SetWindow(base, end - base);
}

static GlobalDescriptorTable::TaskRegisterObservation out;
static const char* why[8];
static uint32_t count = 0;

// The expectations, captured once from the production object before any case mutates anything.
//
// This is not a convenience. Several cases below deliberately corrupt the task state itself, and an expectation read at
// call time would move with the corruption: a case that sets ss0 to a wrong value and then asks "is ss0 wrong?" would be
// comparing the wrong value against itself and would pass while proving nothing. Capturing the baseline once, before the
// first mutation, is what makes each case a comparison against the runtime's state rather than against its own edit.
static uint32_t expectedSelector = 0;
static uint32_t expectedBase = 0;
static uint32_t expectedLimit = 0;
static uint32_t expectedSs0 = 0;
static uint32_t expectedIoMapBase = 0;

static void CaptureExpectations() {
    expectedSelector = Descriptors().TaskStateSelector();
    expectedBase = Descriptors().TaskStateBase();
    expectedLimit = Descriptors().TaskStateLimit();
    expectedSs0 = Descriptors().TaskStateRing0Selector();
    expectedIoMapBase = Descriptors().TaskStateIoMapBase();
}

// One call of the production observation against the captured expectations.
static uint32_t Observe() {
    ResetCounters();
    count = 0;
    why[0] = 0;
    return Descriptors().ObserveTaskRegister(out, expectedSelector, expectedBase, expectedLimit,
        expectedSs0, expectedIoMapBase, why, count);
}

// A refusal decided before the task state is reached must charge no task-state byte at all.
static void RefuseBefore(const char* label, const char* expectedWhy, uint32_t expectedDescriptorReads) {
    const uint32_t bad = Observe();
    Expect(bad > 0, label);
    Expect(bad == 1, "the case broke exactly one condition");
    Expect(count == 1, "exactly one obligation failed");
    Expect(SameText(why[0], expectedWhy), "the first refusal is the expected one");
    Expect(gtos_gdt_host_illegal_reads == 0, "the refusal did not read outside the declared window");
    Expect(gtos_gdt_host_tss_reads == 0, "the refusal read no byte of the task state");
    Expect(gtos_gdt_host_descriptor_reads == expectedDescriptorReads, "the descriptor was read exactly as expected");
    Say("   ");
    Say(label);
    Say(" -> ");
    Say(why[0] ? why[0] : "(unnamed)");
    Say(" desc ");
    SayHex(gtos_gdt_host_descriptor_reads);
    Say(" tss ");
    SayHex(gtos_gdt_host_tss_reads);
    Say(" illegal ");
    SayHex(gtos_gdt_host_illegal_reads);
    Say("\n");
}

// A refusal whose wrong value lives inside the task state can only be decided by reading it, so it must show that read,
// inside the window.
static void RefuseAfter(const char* label, const char* expectedWhy) {
    const uint32_t bad = Observe();
    Expect(bad > 0, label);
    Expect(bad == 1, "the case broke exactly one condition");
    Expect(count == 1, "exactly one obligation failed");
    Expect(SameText(why[0], expectedWhy), "the first refusal is the expected one");
    Expect(gtos_gdt_host_illegal_reads == 0, "the refusal did not read outside the declared window");
    Expect(gtos_gdt_host_descriptor_reads == 8, "the descriptor was read before this refusal");
    Expect(gtos_gdt_host_tss_reads == 8, "the task state was read before this refusal");
    Say("   ");
    Say(label);
    Say(" -> ");
    Say(why[0] ? why[0] : "(unnamed)");
    Say(" desc ");
    SayHex(gtos_gdt_host_descriptor_reads);
    Say(" tss ");
    SayHex(gtos_gdt_host_tss_reads);
    Say("\n");
}

static void PutAccess(uint32_t index, uint8_t access) { DescriptorAt(index)[5] = access; }
static void PutBase(uint32_t index, uint32_t base) {
    uint8_t* d = DescriptorAt(index);
    d[2] = (uint8_t)(base & 0xFF);
    d[3] = (uint8_t)((base >> 8) & 0xFF);
    d[4] = (uint8_t)((base >> 16) & 0xFF);
    d[7] = (uint8_t)((base >> 24) & 0xFF);
}
static void PutLimit(uint32_t index, uint32_t limit) {
    uint8_t* d = DescriptorAt(index);
    d[0] = (uint8_t)(limit & 0xFF);
    d[1] = (uint8_t)((limit >> 8) & 0xFF);
    d[6] = (uint8_t)((d[6] & 0xF0) | ((limit >> 16) & 0x0F));
}
static uint8_t AccessAt(uint32_t index) { return DescriptorAt(index)[5]; }
static uint32_t BaseAt(uint32_t index) {
    const uint8_t* d = DescriptorAt(index);
    return (uint32_t)d[2] | ((uint32_t)d[3] << 8) | ((uint32_t)d[4] << 16) | ((uint32_t)d[7] << 24);
}
static uint32_t LimitAt(uint32_t index) {
    const uint8_t* d = DescriptorAt(index);
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | (((uint32_t)d[6] & 0x0F) << 16);
}

// The task descriptor's index in the production table, taken from the selector the runtime loads.
static uint32_t TaskIndex() { return Descriptors().TaskStateSelector() >> 3; }

extern "C" int oMain() {
    Say("CASE O-SAFE-TSS\n");

    // ---------------------------------------------------------------- the production initialisation
    new (gdtStorage) GlobalDescriptorTable();
    const uint32_t stackTop = (uint32_t)(__SIZE_TYPE__)&kernelStack[sizeof(kernelStack)];
    Descriptors().LoadTaskState(stackTop);

    // The seam supplies what a machine would have returned. These are the runtime's own values, asked of the runtime.
    SetTaskRegister(Descriptors().TaskStateSelector());
    SetGdtRegister(Descriptors().DescriptorTableBase(), Descriptors().DescriptorTableLimit());
    DeclareWindow();
    // Before any case mutates anything, so every comparison below is against the runtime's state.
    CaptureExpectations();

    Say("   selector ");
    SayHex(Descriptors().TaskStateSelector());
    Say(" base ");
    SayHex(Descriptors().TaskStateBase());
    Say(" limit ");
    SayHex(Descriptors().TaskStateLimit());
    Say(" ss0 ");
    SayHex(Descriptors().TaskStateRing0Selector());
    Say(" iomap ");
    SayHex(Descriptors().TaskStateIoMapBase());
    Say(" esp0 ");
    SayHex(Descriptors().TaskStateKernelStack());
    Say("\n");

    // The table this unit observes is the one production built: the descriptor's own fields must be the runtime's state.
    Expect(Descriptors().TaskStateIsLoaded(), "the task state was loaded");
    Expect(Descriptors().TaskStateKernelStack() == stackTop, "the loaded stack top is the one this unit passed");
    Expect(BaseAt(TaskIndex()) == Descriptors().TaskStateBase(), "the production descriptor points at the runtime task state");
    Expect(LimitAt(TaskIndex()) == Descriptors().TaskStateLimit(), "the production descriptor's limit is the task state size");
    Expect((AccessAt(TaskIndex()) & 0x89) == 0x89, "the production descriptor is present and a 32-bit available TSS");
    Expect(Descriptors().TaskStateSelector() == TaskIndex() * 8, "the selector the runtime loads indexes the descriptor it built");

    // ---------------------------------------------------------------- the normal case
    {
        const uint32_t bad = Observe();
        Expect(bad == 0, "the runtime's own state satisfies every obligation");
        Expect(gtos_gdt_host_illegal_reads == 0, "the normal case read nothing outside the declared window");
        // Eight descriptor bytes and eight task-state bytes: esp0 is four, ss0 is two and the io map base is two. The
        // counts are asserted so a counter that never increments cannot make a refusal look clean.
        Expect(gtos_gdt_host_descriptor_reads == 8, "the eight descriptor bytes were read");
        Expect(gtos_gdt_host_tss_reads == 8, "the eight task-state bytes were read");
        Expect(out.tss_ss0 == expectedSs0, "the ring-0 selector was read");
        Expect(out.tss_esp0 == stackTop, "the ring-0 stack pointer is the stack this unit loaded");
        Expect(out.tss_iomap_base == expectedIoMapBase, "the io map base was read");
        Expect(out.descriptor_base == expectedBase, "the observed base is the runtime's task state");
        Expect(out.descriptor_in_gdt == 1, "the descriptor lies inside the GDT");
        Say("   normal: desc ");
        SayHex(gtos_gdt_host_descriptor_reads);
        Say(" tss ");
        SayHex(gtos_gdt_host_tss_reads);
        Say(" illegal ");
        SayHex(gtos_gdt_host_illegal_reads);
        Say("\n");
    }

    // ---------------------------------------------------------------- the selector, each condition alone
    // These are decided before the descriptor is indexed, so they read no descriptor byte and no task-state byte.
    //
    // The runtime loads a ring-0 GDT selector, so a selector with TI or RPL set also differs from the expected value. That
    // is exactly why the observation judges the observed selector on its own terms before it compares values: written the
    // other way round, the equality test would fire first for every one of these and the TI and RPL obligations would be
    // unreachable. Each case below therefore names the obligation it is actually testing, and the value-only case is kept
    // separate so that the equality check is still shown to work on its own.
    SetTaskRegister(0x7FF8);
    RefuseBefore("a well-formed selector that is not the loaded one is refused",
        "the task register selector is not the one that was loaded", 0);
    SetTaskRegister(0x33);
    RefuseBefore("a selector with a non-zero RPL is refused",
        "the task register selector has a ring other than 0", 0);
    // The loaded selector with only the RPL changed: same index, same TI, so only the RPL obligation can fire.
    SetTaskRegister((Descriptors().TaskStateSelector() & ~3u) | 3u);
    RefuseBefore("the loaded selector with only its RPL changed is refused",
        "the task register selector has a ring other than 0", 0);
    // TI = 1 names the LDT. The index is the same and the RPL is zero, so only the TI obligation can fire.
    SetTaskRegister(Descriptors().TaskStateSelector() | 4u);
    RefuseBefore("the loaded selector with TI set is refused",
        "the task register selector names the LDT, not the GDT", 0);
    SetTaskRegister(Descriptors().TaskStateSelector());

    // ---------------------------------------------------------------- the descriptor, each condition alone
    // A descriptor offset past the table limit. The index is inside the selector's range but the descriptor does not fit,
    // so nothing may be read.
    {
        const uint16_t savedLimit = (uint16_t)Descriptors().DescriptorTableLimit();
        // A GDT register whose limit stops before the task descriptor. The selector still names index 6.
        SetGdtRegister(Descriptors().DescriptorTableBase(), (uint16_t)(TaskIndex() * 8 - 1));
        RefuseBefore("a descriptor past the GDT limit is refused",
            "the descriptor is not inside the GDT limit", 0);
        SetGdtRegister(Descriptors().DescriptorTableBase(), savedLimit);
    }

    const uint8_t savedAccess = AccessAt(TaskIndex());
    const uint32_t savedBase = BaseAt(TaskIndex());
    const uint32_t savedLimit = LimitAt(TaskIndex());

    // Absent: clear the present bit, keep everything else.
    PutAccess(TaskIndex(), (uint8_t)(savedAccess & ~0x80u));
    RefuseBefore("an absent descriptor is refused", "the TSS descriptor is not present", 8);
    PutAccess(TaskIndex(), savedAccess);

    // A code segment: S = 1, so not a system descriptor at all.
    PutAccess(TaskIndex(), 0x9A);
    RefuseBefore("a non-system descriptor is refused", "the descriptor is not a system descriptor", 8);
    PutAccess(TaskIndex(), savedAccess);

    // A system descriptor of the wrong type. A 16-bit TSS is type 1 and an LDT is type 2; both keep S = 0, so they reach
    // the type check instead of being refused as code.
    PutAccess(TaskIndex(), (uint8_t)((savedAccess & 0xF0u) | 0x01u));
    RefuseBefore("a 16-bit TSS descriptor is refused",
        "the TSS descriptor type is not 32-bit available TSS", 8);
    PutAccess(TaskIndex(), (uint8_t)((savedAccess & 0xF0u) | 0x02u));
    RefuseBefore("an LDT descriptor is refused",
        "the TSS descriptor type is not 32-bit available TSS", 8);
    PutAccess(TaskIndex(), savedAccess);

    // A non-zero DPL.
    PutAccess(TaskIndex(), (uint8_t)(savedAccess | 0x60u));
    RefuseBefore("a descriptor with a non-zero DPL is refused", "the TSS descriptor DPL is not 0", 8);
    PutAccess(TaskIndex(), savedAccess);

    // A base that is not the runtime's task state. This is the case a window-wide counter could not distinguish: the base
    // is outside the declared window, and the refusal must happen before anything reads there.
    PutBase(TaskIndex(), Descriptors().TaskStateBase() + 0x10000u);
    RefuseBefore("a base outside the declared window is refused without reading it",
        "the TSS descriptor base is not the runtime task state", 8);
    PutBase(TaskIndex(), savedBase);

    // A base that is inside the window but is not the task state: the runtime's own stack, which is a legal address this
    // code could read. It must be refused, and it must read nothing there.
    PutBase(TaskIndex(), (uint32_t)(__SIZE_TYPE__)&kernelStack[0]);
    RefuseBefore("a base inside the window that is not the task state is refused",
        "the TSS descriptor base is not the runtime task state", 8);
    PutBase(TaskIndex(), savedBase);

    // A limit that is not the task state's size.
    PutLimit(TaskIndex(), 0x40);
    RefuseBefore("a descriptor limit that is not the task state size is refused",
        "the TSS descriptor limit is not the task state size", 8);
    PutLimit(TaskIndex(), savedLimit);

    // ---------------------------------------------------------------- the task state, each condition alone
    // A ring-0 selector that is not the runtime's. This one can only be decided by reading the task state, so it must
    // show the read it needed.
    {
        uint8_t* tss = (uint8_t*)Descriptors().TaskStateBase();
        const uint8_t lo = tss[8];
        const uint8_t hi = tss[9];
        tss[8] = 0x20;
        tss[9] = 0x00;
        RefuseAfter("a TSS ring-0 selector that is not the runtime's is refused",
            "the TSS ring-0 stack selector is not the one the runtime set");
        Expect(out.tss_ss0 == 0x20, "the wrong value is what was read");
        tss[8] = lo;
        tss[9] = hi;
    }

    // An io map base that is not the runtime's.
    {
        uint8_t* tss = (uint8_t*)Descriptors().TaskStateBase();
        const uint8_t lo = tss[102];
        const uint8_t hi = tss[103];
        tss[102] = 0;
        tss[103] = 0;
        RefuseAfter("a TSS io map base that is not the runtime's is refused",
            "the TSS io map base is not the one the runtime set");
        tss[102] = lo;
        tss[103] = hi;
    }

    // ---------------------------------------------------------------- several faults at once
    // An absent descriptor whose base is also outside the window. The first refusal must be the absence, and no read may
    // happen at the bad base: the earlier gate wins, and the case says so by name.
    PutAccess(TaskIndex(), (uint8_t)(savedAccess & ~0x80u));
    PutBase(TaskIndex(), Descriptors().TaskStateBase() + 0x20000u);
    {
        const uint32_t bad = Observe();
        Expect(bad == 1, "several faults at once stop at the first one");
        Expect(SameText(why[0], "the TSS descriptor is not present"),
            "the earlier gate wins: the descriptor is judged absent before its base is judged wrong");
        Expect(gtos_gdt_host_illegal_reads == 0, "several faults at once read nothing outside the window");
        Expect(gtos_gdt_host_tss_reads == 0, "several faults at once read no byte of the task state");
        Say("   several faults -> ");
        Say(why[0] ? why[0] : "(unnamed)");
        Say("\n");
    }
    PutAccess(TaskIndex(), savedAccess);
    PutBase(TaskIndex(), savedBase);

    // ---------------------------------------------------------------- back to clean
    {
        const uint32_t bad = Observe();
        Expect(bad == 0, "the restored table satisfies every obligation again");
        Expect(gtos_gdt_host_illegal_reads == 0, "and reads nothing outside the window");
        Expect(gtos_gdt_host_tss_reads == 8, "and reads the task state again");
    }

    Say(failures == 0 ? "SAFE TSS UNIT PASS\n" : "SAFE TSS UNIT FAIL\n");
    return (int)failures;
}

// GTOS-DSH-134 O: the TSS is read only after every precondition holds, and a refusal can be shown not to have read.
//
// The previous packet's observation read the TSS fields at the descriptor's base before it had checked the base, the limit,
// the present bit, the type or the DPL. A test that used `base + 8` as its "bad" case could not tell the difference, because
// `base + 8` was still readable: the refusal fired, but the dangerous read had already happened.
//
// The repair orders the checks so that no address derived from the descriptor is touched until the descriptor has been
// proved to be a present, ring-0, 32-bit available TSS whose base and limit are the caller's own task state and whose limit
// covers the fields being read. On a real machine that ordering is the safety. On the host there is no fault, so this test
// declares the window the code may read and counts every access outside it: a refusal is then proved by the counter, not by
// the address happening to be readable.
//
// The boundary is stated: the host supplies the three machine-level reads (STR, SGDT and the TSS bytes) through declared
// symbols, exactly as the runtime already takes CR0 and CR3. The decoding, the ordering and every refusal are the
// production code.

#include <common/types.h>
#include <gdt.h>

using namespace gtos;

extern "C" {
uint32_t gtos_gdt_host_str_selector = 0;
uint32_t gtos_gdt_host_sgdt_base = 0;
uint32_t gtos_gdt_host_sgdt_limit = 0;
uint32_t gtos_gdt_host_reads = 0;
uint32_t gtos_gdt_host_window_base = 0;
uint32_t gtos_gdt_host_window_bytes = 0;
uint32_t gtos_gdt_host_illegal_reads = 0;
uint32_t gtos_gdt_host_descriptor_reads = 0;
uint32_t gtos_gdt_host_tss_reads = 0;
}

static uint32_t failures = 0;

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

// The table and the task state the observation will be pointed at.
static uint8_t table[8 * 8];
static uint8_t taskState[104];

static void PutDescriptor(uint8_t* at, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    at[0] = (uint8_t)(limit & 0xFF);
    at[1] = (uint8_t)((limit >> 8) & 0xFF);
    at[2] = (uint8_t)(base & 0xFF);
    at[3] = (uint8_t)((base >> 8) & 0xFF);
    at[4] = (uint8_t)((base >> 16) & 0xFF);
    at[5] = access;
    at[6] = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    at[7] = (uint8_t)((base >> 24) & 0xFF);
}

// The runtime's own initialisation state, read from src/gdt.cpp: the task descriptor is built with 0x89, the TSS sets
// ss0 = 0x18 and ioMapBase = 104, and LoadTaskState loads selector 0x30.
static const uint32_t kSelector = 0x30;
static const uint32_t kLimit = 104 - 1;
static const uint32_t kSs0 = 0x18;
static const uint32_t kIoMap = 104;

static uint32_t tssBase = 0;

static void LayOutTheRuntimeTable() {
    tssBase = (uint32_t)(__SIZE_TYPE__)&taskState[0];
    for (uint32_t i = 0; i < sizeof(table); ++i) table[i] = 0;
    for (uint32_t i = 0; i < sizeof(taskState); ++i) taskState[i] = 0;
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);
    taskState[8] = (uint8_t)(kSs0 & 0xFF);
    taskState[9] = (uint8_t)((kSs0 >> 8) & 0xFF);
    taskState[102] = (uint8_t)(kIoMap & 0xFF);
    taskState[103] = (uint8_t)((kIoMap >> 8) & 0xFF);
    gtos_gdt_host_str_selector = kSelector;
    gtos_gdt_host_sgdt_base = (uint32_t)(__SIZE_TYPE__)&table[0];
    gtos_gdt_host_sgdt_limit = sizeof(table) - 1;
    // The window this code may read: the span that holds the table and the task state, and nothing else. The two objects
    // are not adjacent, so the window is their union rather than the table alone.
    const uint32_t tableStart = (uint32_t)(__SIZE_TYPE__)&table[0];
    const uint32_t tableEnd = tableStart + (uint32_t)sizeof(table);
    const uint32_t tssStart = (uint32_t)(__SIZE_TYPE__)&taskState[0];
    const uint32_t tssEnd = tssStart + (uint32_t)sizeof(taskState);
    gtos_gdt_host_window_base = tableStart < tssStart ? tableStart : tssStart;
    const uint32_t windowEnd = tableEnd > tssEnd ? tableEnd : tssEnd;
    gtos_gdt_host_window_bytes = windowEnd - gtos_gdt_host_window_base;
}

extern "C" int oMain() {
    Say("CASE O-SAFE-TSS\n");
    LayOutTheRuntimeTable();

    GlobalDescriptorTable::TaskRegisterObservation out;
    const char* why[8];
    uint32_t count = 0;

    // ---------------------------------------------------------------- the normal case
    {
        gtos_gdt_host_illegal_reads = 0;
        gtos_gdt_host_descriptor_reads = 0;
        gtos_gdt_host_tss_reads = 0;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, kSelector, tssBase,
            kLimit, kSs0, kIoMap, why, count);
        Expect(bad == 0, "the runtime's own state satisfies every obligation");
        Expect(gtos_gdt_host_illegal_reads == 0, "the normal case read nothing outside the declared window");
        // Eight descriptor bytes and eight task-state bytes: esp0 is four, ss0 is two and the io map base is two. The
        // counts are asserted so a counter that never increments cannot make a refusal look clean.
        Expect(gtos_gdt_host_descriptor_reads == 8, "the eight descriptor bytes were read");
        Expect(gtos_gdt_host_tss_reads == 8, "the eight task-state bytes were read");
        Expect(out.tss_ss0 == kSs0, "the ring-0 selector was read");
        Expect(out.tss_iomap_base == kIoMap, "the io map base was read");
        Say("   selector "); SayHex(out.str_selector);
        Say(" base "); SayHex(out.descriptor_base);
        Say(" limit "); SayHex(out.descriptor_limit);
        Say(" ss0 "); SayHex(out.tss_ss0);
        Say(" iomap "); SayHex(out.tss_iomap_base);
        Say(" descriptor reads "); SayHex(gtos_gdt_host_descriptor_reads);
        Say(" tss reads "); SayHex(gtos_gdt_host_tss_reads);
        Say(" illegal reads "); SayHex(gtos_gdt_host_illegal_reads);
        Say("\n");
    }

    // ---------------------------------------------------------------- each precondition, alone
    // Two kinds of refusal, and the test has to say which is which. A refusal that is decided before the task state is
    // reached must show no task-state read at all; a refusal whose wrong value lives inside the task state can only be
    // decided by reading it, and must show that read stayed inside the window. One blanket assertion would be either too
    // weak for the first kind or false for the second.
    auto reset = []() {
        gtos_gdt_host_illegal_reads = 0;
        gtos_gdt_host_descriptor_reads = 0;
        gtos_gdt_host_tss_reads = 0;
    };
    auto refuseBefore = [&](const char* label, uint32_t expectedSelector, uint32_t expectedBase, uint32_t expectedLimit) {
        reset();
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, expectedBase,
            expectedLimit, kSs0, kIoMap, why, count);
        Expect(bad > 0, label);
        Expect(gtos_gdt_host_illegal_reads == 0, "the refusal did not read outside the declared window");
        // No task-state byte at all. This holds even when the base it refused was a legal address inside the window, which
        // is the case a single window-wide counter could not distinguish.
        Expect(gtos_gdt_host_tss_reads == 0, "the refusal read no byte of the task state");
        Say("   ");
        Say(label);
        Say(" -> ");
        Say(count > 0 && why[0] ? why[0] : "(unnamed)");
        Say(" desc "); SayHex(gtos_gdt_host_descriptor_reads);
        Say(" tss "); SayHex(gtos_gdt_host_tss_reads);
        Say(" illegal "); SayHex(gtos_gdt_host_illegal_reads);
        Say("\n");
    };
    auto refuseAfter = [&](const char* label) {
        reset();
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, kSelector, tssBase, kLimit, kSs0, kIoMap,
            why, count);
        Expect(bad > 0, label);
        Expect(gtos_gdt_host_illegal_reads == 0, "the refusal did not read outside the declared window");
        Expect(gtos_gdt_host_tss_reads == 8, "the refusal read the task state once, inside the window");
        Say("   ");
        Say(label);
        Say(" -> ");
        Say(count > 0 && why[0] ? why[0] : "(unnamed)");
        Say(" desc "); SayHex(gtos_gdt_host_descriptor_reads);
        Say(" tss "); SayHex(gtos_gdt_host_tss_reads);
        Say("\n");
    };

    // The descriptor is outside the GDT limit: the offset arithmetic must not wrap, and nothing is read.
    gtos_gdt_host_str_selector = 0x7FF8;
    refuseBefore("a descriptor past the table limit is refused", 0x7FF8, tssBase, kLimit);
    gtos_gdt_host_str_selector = kSelector;

    // The descriptor is absent.
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x09, 0x00);
    refuseBefore("an absent descriptor is refused", kSelector, tssBase, kLimit);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // The descriptor is a code segment, not a system descriptor at all.
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x9A, 0x00);
    refuseBefore("a non-system descriptor is refused", kSelector, tssBase, kLimit);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // A system descriptor of the wrong type: a 16-bit TSS is type 1, an LDT is type 2. Both have S = 0, so they reach the
    // type check rather than being refused as code.
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x81, 0x00);
    refuseBefore("a 16-bit TSS descriptor is refused", kSelector, tssBase, kLimit);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x82, 0x00);
    refuseBefore("an LDT descriptor is refused", kSelector, tssBase, kLimit);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // A non-zero DPL.
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0xE9, 0x00);
    refuseBefore("a descriptor with a non-zero DPL is refused", kSelector, tssBase, kLimit);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // A base that is not the runtime's task state. This is the case the previous packet could not test: the base points
    // outside the declared window, and the refusal must happen before anything reads there.
    PutDescriptor(&table[6 * 8], tssBase + 0x10000u, kLimit, 0x89, 0x00);
    refuseBefore("a base outside the declared window is refused without reading it", kSelector, tssBase, kLimit);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // A limit that does not cover the fields read.
    PutDescriptor(&table[6 * 8], tssBase, 8, 0x89, 0x00);
    refuseBefore("a short limit is refused", kSelector, tssBase, 8);
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // A selector with ring 3.
    gtos_gdt_host_str_selector = 0x33;
    refuseBefore("a selector with a non-zero RPL is refused", kSelector, tssBase, kLimit);
    gtos_gdt_host_str_selector = kSelector;

    // A selector that names the LDT.
    gtos_gdt_host_str_selector = 0x34;
    refuseBefore("a selector that names the LDT is refused", kSelector, tssBase, kLimit);
    gtos_gdt_host_str_selector = kSelector;

    // A TSS whose own ss0 is not the runtime's.
    taskState[8] = 0x20;
    {
        gtos_gdt_host_illegal_reads = 0;
        gtos_gdt_host_descriptor_reads = 0;
        gtos_gdt_host_tss_reads = 0;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, kSelector, tssBase, kLimit, kSs0, kIoMap,
            why, count);
        Expect(bad > 0, "a TSS ring-0 selector that is not the runtime's is refused");
        Expect(out.tss_ss0 == 0x20, "the wrong value is what was read");
        Expect(gtos_gdt_host_illegal_reads == 0, "reading the real TSS stayed inside the window");
        // This refusal is after the read, so both regions were touched. That is the contrast with every refusal above.
        Expect(gtos_gdt_host_descriptor_reads == 8, "the descriptor was read before this refusal");
        Expect(gtos_gdt_host_tss_reads == 8, "the task state was read before this refusal");
    }
    taskState[8] = (uint8_t)(kSs0 & 0xFF);

    // A TSS whose io map base is wrong.
    taskState[102] = 0;
    taskState[103] = 0;
    refuseAfter("a TSS io map base that is not the runtime's is refused");
    taskState[102] = (uint8_t)(kIoMap & 0xFF);
    taskState[103] = (uint8_t)((kIoMap >> 8) & 0xFF);

    // ---------------------------------------------------------------- several faults at once
    // An absent descriptor whose base is also outside the window: the first refusal must be the absence, and no read may
    // happen at the bad base.
    PutDescriptor(&table[6 * 8], tssBase + 0x20000u, kLimit, 0x09, 0x00);
    gtos_gdt_host_illegal_reads = 0;
    gtos_gdt_host_descriptor_reads = 0;
    gtos_gdt_host_tss_reads = 0;
    {
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, kSelector, tssBase, kLimit, kSs0, kIoMap,
            why, count);
        Expect(bad > 0, "several faults at once are refused");
        Expect(gtos_gdt_host_illegal_reads == 0, "several faults at once read nothing outside the window");
        Expect(gtos_gdt_host_tss_reads == 0, "several faults at once read no byte of the task state");
        Expect(count > 0 && why[0] != 0, "the first refusal is named");
        Say("   several faults -> "); Say(why[0]); Say("\n");
    }
    PutDescriptor(&table[6 * 8], tssBase, kLimit, 0x89, 0x00);

    // ---------------------------------------------------------------- back to clean
    {
        gtos_gdt_host_illegal_reads = 0;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, kSelector, tssBase, kLimit, kSs0, kIoMap,
            why, count);
        Expect(bad == 0, "the restored table satisfies every obligation again");
        Expect(gtos_gdt_host_illegal_reads == 0, "and reads nothing outside the window");
    }

    Say(failures == 0 ? "SAFE TSS UNIT PASS\n" : "SAFE TSS UNIT FAIL\n");
    return (int)failures;
}

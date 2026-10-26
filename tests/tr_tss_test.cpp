// GTOS-DSH-131 O: the task register and the TSS descriptor, read back and checked against known state.
//
// The previous packet reported TR and TSS as null because the diagnostic never printed them. The taskbook is explicit that
// this is a missing implementation, not an environment limit: the observation is authorised and is the work of this packet.
//
// What is tested here is the production `ObserveTaskRegister` method, compiled into a host program. The host cannot execute
// STR or SGDT or read a descriptor table, so those three machine-level reads come from declared host symbols - the same
// seam the runtime already uses for CR0/CR3 and the bootstrap identity. Everything above that seam is the production code:
// the selector arithmetic, the descriptor decoding, the TSS field extraction and every obligation.
//
// The expected values are the runtime's own initialisation state, not the descriptor under examination: the selector that
// LoadTaskState loads, the base and limit the constructor gave the descriptor, the ss0 the constructor set, and the io map
// base. A comparison that read its expectations from the descriptor would pass even if the descriptor were wrong.
//
// The boundary is stated rather than hidden: this is a host test of the observation logic. It proves the arithmetic and the
// refusals. It does not prove what a real machine's STR returns; that is what the guest scene is for, and the two are
// reported separately.

#include <common/types.h>
#include <gdt.h>

using namespace gtos;

// The declared host seam.
extern "C" {
uint32_t gtos_gdt_host_str_selector = 0;
uint32_t gtos_gdt_host_sgdt_base = 0;
uint32_t gtos_gdt_host_sgdt_limit = 0;
uint32_t gtos_gdt_host_reads = 0;
}

static uint32_t failures = 0;

static uint32_t Length(const char* text) {
    uint32_t n = 0;
    while (text[n]) ++n;
    return n;
}
static int WriteAll(const char* text, uint32_t length) {
    int result = 0;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(4), "b"(2), "c"(text), "d"(length)
                     : "memory");
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
static void ExpectHex(uint32_t got, uint32_t want, const char* label) {
    if (got == want) return;
    ++failures;
    Say("  FAIL ");
    Say(label);
    Say(" got ");
    SayHex(got);
    Say(" want ");
    SayHex(want);
    Say("\n");
}

// A GDT laid out the way the runtime's own object is, so the observation reads a real table rather than a stub.
struct alignas(8) FakeGdt {
    uint8_t bytes[7 * 8];
    uint8_t tss[104];
};

static uint8_t table[7 * 8 + 104];

// Encode one 32-bit descriptor in the eight bytes at `at`.
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

extern "C" int trTssMain() {
    Say("CASE O-TR-TSS\n");

    // ---------------------------------------------------------------- the runtime's own initialisation state
    // These are the values the production constructor sets, read from src/gdt.cpp: the task descriptor is built with
    // type 0x89 (present, DPL 0, 32-bit available TSS), the TSS sets ss0 = 0x18 and ioMapBase = sizeof(TaskStateSegment),
    // and LoadTaskState loads selector 0x30.
    const uint32_t expectedSelector = 0x30;
    const uint32_t expectedLimit = 104 - 1;      // sizeof(TaskStateSegment) - 1
    const uint32_t expectedSs0 = 0x18;
    const uint32_t expectedIoMapBase = 104;

    const uint32_t tssBase = (uint32_t)(__SIZE_TYPE__)&table[7 * 8];
    // Lay the table out as the runtime does: six descriptors then the task descriptor at index 6 (0x30 >> 3).
    PutDescriptor(&table[0 * 8], 0, 0, 0x00, 0x00);
    PutDescriptor(&table[1 * 8], 0, 0, 0x00, 0x00);
    PutDescriptor(&table[2 * 8], 0, 0xFFFFF, 0x9A, 0xC0);
    PutDescriptor(&table[3 * 8], 0, 0xFFFFF, 0x92, 0xC0);
    PutDescriptor(&table[4 * 8], 0, 0xFFFFF, 0xFA, 0xC0);
    PutDescriptor(&table[5 * 8], 0, 0xFFFFF, 0xF2, 0xC0);
    PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x89, 0x00);
    // The TSS fields the constructor sets.
    for (uint32_t i = 0; i < 104; ++i) table[7 * 8 + i] = 0;
    table[7 * 8 + 8] = (uint8_t)(expectedSs0 & 0xFF);
    table[7 * 8 + 9] = (uint8_t)((expectedSs0 >> 8) & 0xFF);
    table[7 * 8 + 102] = (uint8_t)(expectedIoMapBase & 0xFF);
    table[7 * 8 + 103] = (uint8_t)((expectedIoMapBase >> 8) & 0xFF);

    // The GDT register points at the table and its limit is the last valid byte.
    gtos_gdt_host_sgdt_base = (uint32_t)(__SIZE_TYPE__)&table[0];
    gtos_gdt_host_sgdt_limit = 7 * 8 - 1;
    gtos_gdt_host_str_selector = expectedSelector;

    GlobalDescriptorTable::TaskRegisterObservation out;
    const char* why[8];
    uint32_t count = 0;

    // ---------------------------------------------------------------- the normal case
    {
        const uint32_t readsBefore = gtos_gdt_host_reads;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        ExpectHex(bad, 0, "the runtime's own state satisfies every obligation");
        ExpectHex(count, 0, "no failure was recorded");
        ExpectHex(gtos_gdt_host_reads - readsBefore, 1, "the machine-level reads were consulted once");
        ExpectHex(out.read_ok, 1, "the reads executed");
        ExpectHex(out.str_selector, 0x30, "STR returned the loaded selector");
        ExpectHex(out.str_index, 6, "the selector indexes descriptor 6");
        ExpectHex(out.str_ti, 0, "the selector names the GDT");
        ExpectHex(out.str_rpl, 0, "the selector's ring is 0");
        ExpectHex(out.descriptor_offset, 0x30, "the descriptor sits at offset 0x30");
        ExpectHex(out.descriptor_in_gdt, 1, "the descriptor is inside the GDT limit");
        ExpectHex(out.descriptor_present, 1, "the descriptor is present");
        ExpectHex(out.descriptor_type & 0x0F, 9, "the type is 32-bit available TSS");
        ExpectHex((out.descriptor_type >> 5) & 3, 0, "the DPL is 0");
        ExpectHex(out.descriptor_base, tssBase, "the base is the runtime task state");
        ExpectHex(out.descriptor_limit, expectedLimit, "the limit is the task state size");
        ExpectHex(out.tss_ss0, 0x18, "the TSS ring-0 selector was read back");
        ExpectHex(out.tss_iomap_base, 104, "the TSS io map base was read back");
        Say("   selector ");
        SayHex(out.str_selector);
        Say(" base ");
        SayHex(out.descriptor_base);
        Say(" limit ");
        SayHex(out.descriptor_limit);
        Say(" ss0 ");
        SayHex(out.tss_ss0);
        Say(" iomap ");
        SayHex(out.tss_iomap_base);
        Say("\n");
    }

    // ---------------------------------------------------------------- each obligation, broken alone
    // A different selector in the task register.
    {
        gtos_gdt_host_str_selector = 0x38;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a task register selector that is not the loaded one is refused");
        Expect(count > 0 && why[0] != 0, "the refusal is named");
        Say("   wrong selector -> ");
        Say(why[0] ? why[0] : "(unnamed)");
        Say("\n");
        gtos_gdt_host_str_selector = expectedSelector;
    }
    // A selector with ring 3, which is not a valid task register value.
    {
        gtos_gdt_host_str_selector = 0x33;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a task register selector with a non-zero RPL is refused");
        gtos_gdt_host_str_selector = expectedSelector;
    }
    // A selector that indexes past the GDT limit.
    {
        gtos_gdt_host_str_selector = 0x7FF8;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a descriptor outside the GDT limit is refused");
        ExpectHex(out.descriptor_in_gdt, 0, "the out-of-range descriptor is reported as outside the table");
        gtos_gdt_host_str_selector = expectedSelector;
    }
    // An absent descriptor.
    {
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x09, 0x00);   // present bit cleared
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "an absent TSS descriptor is refused");
        ExpectHex(out.descriptor_present, 0, "the absence is reported");
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x89, 0x00);
    }
    // The wrong descriptor type: a code segment where the TSS should be.
    {
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x9A, 0x00);
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a descriptor that is not a TSS is refused");
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x89, 0x00);
    }
    // A base that is not the runtime's task state.
    {
        PutDescriptor(&table[6 * 8], tssBase + 8, expectedLimit, 0x89, 0x00);
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a TSS base that is not the runtime task state is refused");
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x89, 0x00);
    }
    // A TSS whose ss0 is not the one the runtime set.
    {
        table[7 * 8 + 8] = 0x20;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a TSS ring-0 selector that is not the runtime's is refused");
        ExpectHex(out.tss_ss0, 0x20, "the wrong value is what was read");
        table[7 * 8 + 8] = (uint8_t)(expectedSs0 & 0xFF);
    }
    // A TSS whose io map base is wrong, which would let user port access through.
    {
        table[7 * 8 + 102] = 0x00;
        table[7 * 8 + 103] = 0x00;
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a TSS io map base that is not the runtime's is refused");
        table[7 * 8 + 102] = (uint8_t)(expectedIoMapBase & 0xFF);
        table[7 * 8 + 103] = (uint8_t)((expectedIoMapBase >> 8) & 0xFF);
    }
    // A limit that is not the task state size.
    {
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit + 8, 0x89, 0x00);
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        Expect(bad > 0, "a TSS limit that is not the task state size is refused");
        PutDescriptor(&table[6 * 8], tssBase, expectedLimit, 0x89, 0x00);
    }

    // ---------------------------------------------------------------- back to clean
    {
        uint32_t bad = GlobalDescriptorTable().ObserveTaskRegister(out, expectedSelector, tssBase,
            expectedLimit, expectedSs0, expectedIoMapBase, why, count);
        ExpectHex(bad, 0, "the restored table satisfies every obligation again");
    }

    Say(failures == 0 ? "TR TSS UNIT PASS\n" : "TR TSS UNIT FAIL\n");
    return (int)failures;
}

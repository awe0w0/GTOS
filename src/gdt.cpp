#include <gdt.h>

using namespace gtos;

GlobalDescriptorTable::GlobalDescriptorTable()
    : nullSegmentSelector(0, 0, 0), unusedSegmentSelector(0, 0, 0),
      codeSegmentSelector(0, 0xFFFFFFFF, 0x9A), dataSegmentSelector(0, 0xFFFFFFFF, 0x92),
      userCodeSegmentSelector(0, 0xFFFFFFFF, 0xFA),
      userDataSegmentSelector(0, 0xFFFFFFFF, 0xF2),
      taskSegmentSelector((uint32_t)&taskState, sizeof(TaskStateSegment) - 1, 0x89),
      taskStateLoaded(false) {
    static_assert(sizeof(TaskStateSegment) == 104, "i386 TSS layout");
    for (uint32_t i = 0; i < sizeof(taskState); ++i) ((uint8_t*)&taskState)[i] = 0;
    taskState.ss0 = 0x18;
    taskState.ioMapBase = sizeof(TaskStateSegment);

    struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base;
    } gdtr = {(uint16_t)(7 * sizeof(SegmentDescriptor) - 1), (uint32_t)this};
#ifndef GTOS_NATIVE_RUNTIME_HOST_TEST
    asm volatile("lgdt %0" : : "m"(gdtr) : "memory");
    asm volatile("mov $0x18, %%ax; mov %%ax, %%ds; mov %%ax, %%es; "
                 "mov %%ax, %%fs; mov %%ax, %%gs; mov %%ax, %%ss; "
                 "ljmp $0x10, $1f; 1:"
                 :
                 :
                 : "eax", "memory");
#else
    // A host cannot load a descriptor table or reload the segment registers. The table's contents are still built above,
    // and the selectors below are still the object's real offsets, which is all Activate needs.
    (void)gdtr;
#endif
}

GlobalDescriptorTable::~GlobalDescriptorTable() {}

uint16_t GlobalDescriptorTable::DataSegmentSelector() {
    return (uint8_t *)&dataSegmentSelector - (uint8_t *)this;
}

uint16_t GlobalDescriptorTable::CodeSegmentSelector() {
    return (uint8_t *)&codeSegmentSelector - (uint8_t *)this;
}

GlobalDescriptorTable::SegmentDescriptor::SegmentDescriptor(uint32_t base, uint32_t limit,
                                                            uint8_t flags) {
    uint8_t *target = (uint8_t *)this;

    if (limit <= 65536) {
        // 16-bit address space - yay!
        // 64K of memory should be enough for anybody
        // (640K? Are you kidding me, Bill?)
        target[6] = (flags & 0x10) ? 0x40 : 0;
    } else {
        // 32-bit address space - booo!
        // Now we have to squeeze the (32-bit) limit into 2.5 regiters (20-bit).
        // This is done by discarding the 12 least significant bits, but this
        // is only legal, if they are all ==1, so they are implicitly still there

        // so if the last bits aren't all 1, we have to set them to 1, but this
        // would increase the limit (cannot do that, because we might go beyond
        // the physical limit) so we have to compensate this by decreasing a
        // higher bit (and might have some wasted bytes behind the used memory)

        if ((limit & 0xFFF) != 0xFFF)
            limit = (limit >> 12) - 1;
        else
            limit = limit >> 12;

        target[6] = 0xC0;
    }

    // Encode the limit
    target[0] = limit & 0xFF;
    target[1] = (limit >> 8) & 0xFF;
    target[6] |= (limit >> 16) & 0xF;

    // Encode the base
    target[2] = base & 0xFF;
    target[3] = (base >> 8) & 0xFF;
    target[4] = (base >> 16) & 0xFF;
    target[7] = (base >> 24) & 0xFF;

    // And... Type
    target[5] = flags;
}

uint32_t GlobalDescriptorTable::SegmentDescriptor::Base() {
    uint8_t *target = (uint8_t *)this;
    uint32_t result = target[7];
    result = (result << 8) + target[4];
    result = (result << 8) + target[3];
    result = (result << 8) + target[2];
    return result;
}

uint32_t GlobalDescriptorTable::SegmentDescriptor::Limit() {
    uint8_t *target = (uint8_t *)this;
    uint32_t result = target[6] & 0xF;
    result = (result << 8) + target[1];
    result = (result << 8) + target[0];

    if ((target[6] & 0xC0) == 0xC0) {
        result = (result << 12) | 0xFFF;
    }

    return result;
}
uint16_t GlobalDescriptorTable::UserCodeSegmentSelector() { return 0x23; }
uint16_t GlobalDescriptorTable::UserDataSegmentSelector() { return 0x2B; }
void GlobalDescriptorTable::SetKernelStack(uint32_t top) { taskState.esp0 = top; }
#ifdef GTOS_NATIVE_RUNTIME_HOST_TEST
// The host cannot execute STR, SGDT or read a descriptor table, so the host test supplies the three machine-level reads
// through these symbols and the observation code below is the production code. The host never claims to have observed
// hardware: it supplies what a machine would have returned, and the checks are the same checks.
extern "C" uint32_t gtos_gdt_host_str_selector;
extern "C" uint32_t gtos_gdt_host_sgdt_base;
extern "C" uint32_t gtos_gdt_host_sgdt_limit;
extern "C" uint32_t gtos_gdt_host_reads;
// The declared readable window and the count of accesses outside it. The host cannot fault, so this is how a refusal is
// shown not to have read a bad address.
extern "C" uint32_t gtos_gdt_host_window_base;
extern "C" uint32_t gtos_gdt_host_window_bytes;
extern "C" uint32_t gtos_gdt_host_illegal_reads;
#endif

#ifdef GTOS_NATIVE_RUNTIME_HOST_TEST
// Reads one byte, recording an access outside the declared window instead of performing it. A refusal is then provable by
// the counter rather than by hoping the address happened to be readable.
static uint8_t SafeReadByte(uint64_t address) {
    const uint64_t base = gtos_gdt_host_window_base, bytes = gtos_gdt_host_window_bytes;
    if (address < base || address >= base + bytes) { ++gtos_gdt_host_illegal_reads; return 0; }
    return *(const uint8_t*)(__SIZE_TYPE__)address;
}
#else
static uint8_t SafeReadByte(uint64_t address) { return *(const uint8_t*)(__SIZE_TYPE__)address; }
#endif

uint32_t GlobalDescriptorTable::ObserveTaskRegister(TaskRegisterObservation& out,
                                                    uint32_t expectedSelector, uint32_t expectedDescriptorBase,
                                                    uint32_t expectedDescriptorLimit, uint32_t expectedSs0,
                                                    uint32_t expectedIoMapBase,
                                                    const char* failures[8], uint32_t& failureCount) {
    for (uint32_t i = 0; i < sizeof(out); ++i) ((uint8_t*)&out)[i] = 0;
    failureCount = 0;
    const uint32_t before = failureCount;
    auto fail = [&](const char* name) { if (failureCount < 8) failures[failureCount] = name; ++failureCount; };

    uint16_t str = 0;
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) gdtr;
    gdtr.limit = 0; gdtr.base = 0;
#ifdef GTOS_NATIVE_RUNTIME_HOST_TEST
    str = (uint16_t)gtos_gdt_host_str_selector;
    gdtr.limit = (uint16_t)gtos_gdt_host_sgdt_limit;
    gdtr.base = gtos_gdt_host_sgdt_base;
    ++gtos_gdt_host_reads;
#else
    asm volatile("str %0" : "=r"(str) : : "memory");
    asm volatile("sgdt %0" : "=m"(gdtr) : : "memory");
#endif
    out.read_ok = 1;
    out.str_selector = str;
    out.str_index = (uint32_t)(str >> 3);
    out.str_ti = (uint32_t)((str >> 2) & 1u);
    out.str_rpl = (uint32_t)(str & 3u);
    out.gdt_base = gdtr.base;
    out.gdt_limit = gdtr.limit;

    // ---------------------------------------------------------------- the descriptor, read only when it is safe to
    // A descriptor occupies eight bytes. The offset is computed in 64 bits so a selector near the top of the range cannot
    // wrap the addition, and the whole descriptor must lie inside the table the GDT register reported.
    const uint64_t descriptorOffset = (uint64_t)out.str_index * 8u;
    const uint64_t tableLimit = (uint64_t)out.gdt_limit;
    out.descriptor_offset = (uint32_t)descriptorOffset;
    out.descriptor_in_gdt = (descriptorOffset + 7u <= tableLimit) ? 1u : 0u;
    if (!out.descriptor_in_gdt) {
        // Nothing beyond this point may touch memory addressed by the descriptor: the offset is outside the table, so the
        // descriptor itself is unreadable and the TSS it might have named is not something this code is entitled to read.
        fail("the descriptor is not inside the GDT limit");
        (void)before;
        return failureCount;
    }

    const uint64_t descriptorAddress = (uint64_t)out.gdt_base + descriptorOffset;
    const uint32_t limitLo = (uint32_t)SafeReadByte(descriptorAddress + 0) | ((uint32_t)SafeReadByte(descriptorAddress + 1) << 8);
    const uint32_t baseLo = (uint32_t)SafeReadByte(descriptorAddress + 2) | ((uint32_t)SafeReadByte(descriptorAddress + 3) << 8);
    const uint32_t baseMid = (uint32_t)SafeReadByte(descriptorAddress + 4);
    const uint32_t access = (uint32_t)SafeReadByte(descriptorAddress + 5);
    const uint32_t flagsLimitHi = (uint32_t)SafeReadByte(descriptorAddress + 6);
    const uint32_t baseHi = (uint32_t)SafeReadByte(descriptorAddress + 7);
    out.descriptor_type = access;
    out.descriptor_present = (access & 0x80u) ? 1u : 0u;
    out.descriptor_limit = limitLo | ((flagsLimitHi & 0x0Fu) << 16);
    out.descriptor_base = baseLo | (baseMid << 16) | (baseHi << 24);

    // ---------------------------------------------------------------- the descriptor must be the right kind of thing
    // before its base is used as an address. A descriptor that is absent, not a system descriptor, not a 32-bit available
    // TSS, or not ring 0 describes something this code must not follow. Each refusal returns without dereferencing the
    // base, so a wrong descriptor cannot cause a read at an address it was never entitled to name.
    const bool system = (access & 0x10u) == 0;
    const uint32_t kind = access & 0x0Fu;
    const uint32_t dpl = (access >> 5) & 3u;
    if (!out.descriptor_present) { fail("the TSS descriptor is not present"); (void)before; return failureCount; }
    if (!system) { fail("the descriptor is not a system descriptor"); (void)before; return failureCount; }
    if (kind != 9u) { fail("the TSS descriptor type is not 32-bit available TSS"); (void)before; return failureCount; }
    if (dpl != 0u) { fail("the TSS descriptor DPL is not 0"); (void)before; return failureCount; }

    // The TSS the descriptor names has to be the object this runtime owns. Its base and limit are compared to the caller's
    // own initialisation state before a single byte is read, so a descriptor that points somewhere else is refused rather
    // than followed.
    if (out.descriptor_base != expectedDescriptorBase) {
        fail("the TSS descriptor base is not the runtime task state");
        (void)before; return failureCount;
    }
    if (out.descriptor_limit != expectedDescriptorLimit) {
        fail("the TSS descriptor limit is not the task state size");
        (void)before; return failureCount;
    }
    // The TSS fields this reads lie at offsets 4, 8 and 102 inside a 104-byte structure, so the declared limit must cover
    // the last of them before any of them is read.
    if (out.descriptor_limit < 103u) {
        fail("the TSS limit does not cover the fields this reads");
        (void)before; return failureCount;
    }

    // ---------------------------------------------------------------- now the TSS may be read
    const uint64_t tssAddress = (uint64_t)out.descriptor_base;
    out.tss_esp0 = (uint32_t)SafeReadByte(tssAddress + 4) | ((uint32_t)SafeReadByte(tssAddress + 5) << 8)
                 | ((uint32_t)SafeReadByte(tssAddress + 6) << 16) | ((uint32_t)SafeReadByte(tssAddress + 7) << 24);
    out.tss_ss0 = (uint32_t)SafeReadByte(tssAddress + 8) | ((uint32_t)SafeReadByte(tssAddress + 9) << 8);
    out.tss_iomap_base = (uint32_t)SafeReadByte(tssAddress + 102) | ((uint32_t)SafeReadByte(tssAddress + 103) << 8);

    // The obligations that need the values just read.
    if (!out.read_ok) fail("the machine-level reads did not execute");
    if (out.str_selector != expectedSelector) fail("the task register selector is not the one that was loaded");
    if (out.str_ti != 0) fail("the task register selector names the LDT, not the GDT");
    if (out.str_rpl != 0) fail("the task register selector has a ring other than 0");
    if (out.tss_ss0 != expectedSs0) fail("the TSS ring-0 stack selector is not the one the runtime set");
    if (out.tss_iomap_base != expectedIoMapBase) fail("the TSS io map base is not the one the runtime set");
    (void)before;
    return failureCount;
}

void GlobalDescriptorTable::LoadTaskState(uint32_t top) {
    SetKernelStack(top);
    if (!taskStateLoaded) {
        uint16_t selector = 0x30;
        #ifndef GTOS_NATIVE_RUNTIME_HOST_TEST
        asm volatile("ltr %0" : : "r"(selector) : "memory");
        #endif
        taskStateLoaded = true;
    }
}

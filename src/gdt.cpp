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
    asm volatile("lgdt %0" : : "m"(gdtr) : "memory");
    asm volatile("mov $0x18, %%ax; mov %%ax, %%ds; mov %%ax, %%es; "
                 "mov %%ax, %%fs; mov %%ax, %%gs; mov %%ax, %%ss; "
                 "ljmp $0x10, $1f; 1:"
                 :
                 :
                 : "eax", "memory");
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
void GlobalDescriptorTable::LoadTaskState(uint32_t top) {
    SetKernelStack(top);
    if (!taskStateLoaded) {
        uint16_t selector = 0x30;
        asm volatile("ltr %0" : : "r"(selector) : "memory");
        taskStateLoaded = true;
    }
}

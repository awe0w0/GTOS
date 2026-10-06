#ifndef __GTOS__GDT_H
#define __GTOS__GDT_H

#include <common/types.h>

namespace gtos {
    class GlobalDescriptorTable {
        public:
            class SegmentDescriptor {
                private:
                    uint16_t limit_lo;
                    uint16_t base_lo;
                    uint8_t base_hi;
                    uint8_t type;
                    uint8_t flags_limit_hi;
                    uint8_t base_vhi;
                public:
                    SegmentDescriptor(uint32_t base,uint32_t limit,uint8_t type);
                    uint32_t Base();
                    uint32_t Limit();
            } __attribute__((packed));
            
            SegmentDescriptor nullSegmentSelector;
            SegmentDescriptor unusedSegmentSelector;
            SegmentDescriptor codeSegmentSelector;
            SegmentDescriptor dataSegmentSelector;
            SegmentDescriptor userCodeSegmentSelector;
            SegmentDescriptor userDataSegmentSelector;
            SegmentDescriptor taskSegmentSelector;
        private:
            // An absent bitmap (base beyond limit) denies every user port access.
            struct TaskStateSegment {
                uint32_t previous, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
                uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
                uint32_t es, cs, ss, ds, fs, gs, ldt;
                uint16_t trap, ioMapBase;
            } __attribute__((packed));
            TaskStateSegment taskState;
            bool taskStateLoaded;
        public:
            GlobalDescriptorTable();
            ~GlobalDescriptorTable();

            uint16_t CodeSegmentSelector();
            uint16_t DataSegmentSelector();
            uint16_t UserCodeSegmentSelector();
            uint16_t UserDataSegmentSelector();
            void LoadTaskState(uint32_t kernelStackTop);
            void SetKernelStack(uint32_t kernelStackTop);
    };
}


#endif
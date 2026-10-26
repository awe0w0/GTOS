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

            // A read-only view of what the task register and the TSS descriptor actually say, taken by executing the
            // instructions that read them. Nothing here is stored at load time and echoed back: STR and SGDT read the
            // hardware's own copy, and the descriptor bytes are read from the GDT the base points at.
            struct TaskRegisterObservation {
                uint32_t read_ok;            // 1 when both STR and SGDT executed
                uint32_t str_selector;       // the task register's selector as STR returned it
                uint32_t str_index;          // selector >> 3
                uint32_t str_ti;             // (selector >> 2) & 1
                uint32_t str_rpl;            // selector & 3
                uint32_t gdt_base;           // SGDT base
                uint32_t gdt_limit;          // SGDT limit, in bytes
                uint32_t descriptor_offset;  // the byte offset of the TSS descriptor within the GDT
                uint32_t descriptor_in_gdt;  // 1 when the descriptor lies wholly inside the GDT limit
                uint32_t descriptor_type;    // access byte: present, DPL, type
                uint32_t descriptor_present;
                uint32_t descriptor_limit;
                uint32_t descriptor_base;
                uint32_t tss_ss0;            // the TSS's own ring-0 stack selector
                uint32_t tss_esp0;           // the TSS's own ring-0 stack pointer
                uint32_t tss_iomap_base;
            };

            // Reads the task register, the GDT register and the TSS descriptor. The expected values are supplied by the
            // caller from its own initialisation state, so a comparison cannot be satisfied by copying the descriptor
            // being examined. Returns the number of obligations that failed, and fills failures with their names.
            uint32_t ObserveTaskRegister(TaskRegisterObservation& out,
                                         uint32_t expectedSelector, uint32_t expectedDescriptorBase,
                                         uint32_t expectedDescriptorLimit, uint32_t expectedSs0,
                                         uint32_t expectedIoMapBase,
                                         const char* failures[8], uint32_t& failureCount);
    };
}


#endif
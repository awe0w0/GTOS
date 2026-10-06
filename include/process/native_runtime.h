#ifndef __GTOS__PROCESS__NATIVE_RUNTIME_H
#define __GTOS__PROCESS__NATIVE_RUNTIME_H
#include <memory/process_address_space.h>
#include <multitasking.h>
#include <process/abi.h>
namespace gtos { namespace process {
    // A bounded kernel-provided isolation fixture. This is NOT an ELF loader.
    struct NativeImage {
        const void* code;
        uint32_t codeBytes, entryOffset;
        const void* data;
        uint32_t dataBytes;
    };
    struct NativeStatus {
        uint32_t id, directory, exitCode, faultVector, faultError, faultAddress;
        uint32_t observedCs, observedCr3, observedEflags, systemCalls;
        uint32_t lastWriteResult;
        bool live, reaped;
        TaskStatistics statistics;
    };
    struct NativeStatistics {
        uint32_t created, exited, faulted, reaped, writtenBytes;
    };
    // BSP-only bounded prototype. The runtime, GDT, physical allocator and
    // template must all outlive every process. Kernel stack slots are retained.
    class NativeRuntime {
    public:
        static const uint32_t MaximumProcesses = 4;
        static const uint32_t CodeAddress = 0x40000000U;
        static const uint32_t DataAddress = 0x40002000U;
        static const uint32_t UserStackBottom = 0xBFFFD000U;
        static const uint32_t UserStackTop = 0xBFFFF000U;
        static const uint32_t KernelStackArena = 0xD0000000U;
        static const uint32_t KernelStackPages = 4;
    private:
        struct Slot {
            Task task;
            memory::ProcessAddressSpace space;
            NativeStatus status;
            uint32_t stackFrames[KernelStackPages];
            bool occupied;
            Slot() : task((uint16_t)0, 0), occupied(false) {}
        };
        Slot slots[MaximumProcesses];
        memory::KernelPaging* paging;
        memory::PhysicalMemoryManager* frames;
        TaskManager* scheduler;
        GlobalDescriptorTable* gdt;
        bool stacksPrepared, enabled;
        uint32_t nextId;
        NativeStatistics statistics;
        uint8_t bounce[GTOS_NATIVE_WRITE_LIMIT + 1];
        static NativeRuntime* active;
        Slot* Current();
        void Observe(Slot&, const CPUState&);
        CPUState* Stop(Slot&, CPUState*, uint32_t code);
        bool SafeKernelContext() const;
        bool TrustedImage(const uint8_t*, uint32_t) const;
        Slot* FreeSlot(uint32_t& index);
        bool Admit(Slot&, uint32_t index, uint32_t entry, uint32_t& id);
        NativeRuntime(const NativeRuntime&);
        NativeRuntime& operator=(const NativeRuntime&);
    public:
        NativeRuntime();
        // Call after prepareIdentity, before enable/AP sharing. Guard aliases
        // are retained permanently; no alias changes after sealing.
        bool PrepareStacks(memory::KernelPaging&, memory::PhysicalMemoryManager&);
        // Call after enable + sealForSharedProcessors, on BSP kernel CR3, IF=0.
        bool Activate(TaskManager&, GlobalDescriptorTable&, memory::KernelPaging&,
                      memory::PhysicalMemoryManager&);
        bool Create(const NativeImage&, uint32_t& id);
        // Validated static ET_EXEC only. The trusted source is consumed/copied
        // synchronously and is not retained. Stack+guards cannot overlap loads.
        bool CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id);
        // Boot/desktop context only, never from a trap or on a victim stack.
        uint32_t Reap();
        bool RequestExit(uint32_t id, uint32_t code);
        bool Status(uint32_t id, NativeStatus&) const;
        bool ReadMemory(uint32_t id, uint32_t address, void* out, uint32_t bytes) const;
        NativeStatistics Statistics() const;
        static NativeRuntime* Active();
        CPUState* HandleSyscall(CPUState*);
        // Null means not a supported verified user-origin fault: caller panics.
        CPUState* HandleFault(CPUState*, uint32_t address);
    };
} }
#endif

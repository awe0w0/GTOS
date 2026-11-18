#ifndef __GTOS__PROCESS__NATIVE_RUNTIME_H
#define __GTOS__PROCESS__NATIVE_RUNTIME_H
#include <memory/process_address_space.h>
#include <multitasking.h>
#include <process/abi.h>
#include <process/native_fp.h>
#include <process/native_realtime.h>
#include <process/native_file_endpoint.h>
#include <process/native_loopback_endpoint.h>
#include <process/native_tcp_endpoint.h>
#include <process/native_event_endpoint.h>
#include <process/native_shared_memory_endpoint.h>
#include <process/native_environment.h>
#include <process/thread_abi.h>
#include <process/thread_slot_abi.h>
#include <process/elf32.h>
namespace gtos { namespace process {
    // A bounded kernel-provided isolation fixture. This is NOT an ELF loader.
    struct NativeImage {
        const void* code;
        uint32_t codeBytes, entryOffset;
        const void* data;
        uint32_t dataBytes;
    };
    // Trusted kernel launch choice; never accepted from a user syscall.
    struct NativeElfLaunchProfile {
        uint32_t imagePageLimit, stackPages, runtimePages;
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
    // template must remain alive after activation (there is no deactivation API).
    // Kernel stack slots and shared mappings are retained for the kernel lifetime.
    class NativeRuntime {
        friend struct ::gtos::NativeClockFixture;
    public:
        static const uint32_t MaximumProcesses = GTOS_THREAD_SLOT_LIMIT;
        static const uint32_t CodeAddress = 0x40000000U;
        static const uint32_t DataAddress = 0x40002000U;
        static const uint32_t UserStackBottom = 0xBFFFD000U;
        static const uint32_t UserStackTop = 0xBFFFF000U;
    #ifdef GTOS_NATIVE_RUNTIME_HOST_TEST
    // A kernel virtual address only exists because an MMU translates it, and a 32-bit host process cannot map above its own
    // TASK_SIZE. The host unit therefore places the arena inside the modelled identity window. The layout below -- one
    // leading guard, four pages, one inter-slot guard per slot, one trailing guard -- is unchanged.
    //
    // The address is above the unit's own image rather than at 7 MiB, because the unit links the runtime, its slot table and
    // the surface and window units, and that image ends at 7.06 MiB: an arena at 7 MiB would sit inside the image's own
    // storage. The arena is placed past the image and still below the modelled free frames at 8 MiB, and the unit checks that
    // ordering before it runs a case.
    static const uint32_t KernelStackArena = 0x00780000U;
#else
    static const uint32_t KernelStackArena = 0xD0000000U;
#endif
        static const uint32_t KernelStackPages = 4;
    private:
        struct Slot {
            Task task;
            memory::ProcessAddressSpace space;
            NativeStatus status;
            NativeFpRecord fp;
            NativeEnvironment environment;
            uint32_t stackFrames[KernelStackPages];
            Slot* process;
            uint32_t userStackBegin, userStackEnd, stackHandle;
            uint32_t eventWaitHandles[GTOS_EVENT_WAIT_LIMIT], eventWaitCount, eventWaitSleepCount;
            CPUState* eventWaitFrame;
            struct CodeRange { uint32_t begin, end; } code[Elf32MaximumSegments];
            struct DataRange { uint32_t begin, end; } data[Elf32MaximumSegments];
            uint32_t codeCount, dataCount;
            bool occupied, content;
            Slot() : task((uint16_t)0, 0), process(this), userStackBegin(UserStackBottom),
                userStackEnd(UserStackTop), stackHandle(0), eventWaitCount(0), eventWaitSleepCount(0), eventWaitFrame(0),
                codeCount(0), dataCount(0), occupied(false), content(false) {}
            memory::ProcessAddressSpace& Space() { return process->space; }
            const memory::ProcessAddressSpace& Space() const { return process->space; }
        };
        Slot defaultSlots[MaximumProcesses];
        Slot* slots;
        uint32_t slotCount;
        memory::KernelPaging* paging;
        memory::PhysicalMemoryManager* frames;
        TaskManager* scheduler;
        GlobalDescriptorTable* gdt;
        bool stacksPrepared, enabled;
        bool threadsEnabled;
        uint32_t nextId;
        NativeStatistics statistics;
        NativeFp fp;
        NativeRealtimeClock realtime;
        NativeFileEndpoint* files;
        NativeLoopbackEndpoint* loopback;
        NativeTcpEndpoint* tcp;
        NativeEventEndpoint* events;
        NativeSharedMemoryEndpoint* sharedMemory;
        uint8_t bounce[GTOS_NATIVE_WRITE_LIMIT + 1];
        static NativeRuntime* active;
        Slot* Current(uint32_t* directory = 0);
        void Observe(Slot&, const CPUState&, uint32_t directory);
        CPUState* Stop(Slot&, CPUState*, uint32_t code);
        void StopGroup(Slot&, uint32_t code);
        int CreateThread(Slot&, const GtosThreadCreateRequest&);
        int TryJoin(Slot&, const GtosThreadJoinRequest&);
        CPUState* WaitForEvents(Slot&, CPUState*);
        void WakeEventWaiters();
        static void EventNotification(void*);
        bool StackPinned(const Slot&, uint32_t handle) const;
        bool ExecutableEntry(const Slot&, uint32_t entry) const;
        bool LoadedDataRange(const Slot&, uint32_t address, uint32_t bytes) const;
        bool SafeKernelContext() const;
        bool TrustedImage(const uint8_t*, uint32_t, uint32_t) const;
        Slot* FreeSlot(uint32_t& index);
        bool Admit(Slot&, uint32_t index, uint32_t entry, uint32_t& id, uint32_t stackPages = 2,
                   const NativeEnvironment* environment = 0);
        enum ElfLaunchKind { OrdinaryElf, ContentElf };
        bool CreateElfWithBudget(const uint8_t*, uint32_t bytes, uint32_t& id,
                                 uint32_t imagePageLimit, uint32_t stackPages, uint32_t runtimePages,
                                 const NativeEnvironment* environment = 0,
                                 ElfLaunchKind kind = OrdinaryElf);
        bool LoadElf(const Elf32LoadPlan&, const uint8_t* memoryImage, uint32_t& id,
                     uint32_t stackPages, uint32_t runtimePages, const NativeEnvironment*,
                     ElfLaunchKind, Elf32ReadAt read = 0, void* source = 0,
                     bool (*close)(void*) = 0);
        NativeRuntime(const NativeRuntime&);
        NativeRuntime& operator=(const NativeRuntime&);
    public:
        NativeRuntime();
        // Non-owning endpoint must outlive activation; attach only once, before Activate.
        bool AttachFiles(NativeFileEndpoint& endpoint);
        bool AttachLoopback(NativeLoopbackEndpoint& endpoint);
        bool AttachTcp(NativeTcpEndpoint& endpoint);
        bool AttachEvents(NativeEventEndpoint& endpoint);
        bool AttachSharedMemory(NativeSharedMemoryEndpoint& endpoint);
        // Call after prepareIdentity, before enable/AP sharing. Guard aliases
        // are retained permanently; no alias changes after sealing.
        bool PrepareStacks(memory::KernelPaging&, memory::PhysicalMemoryManager&,
                           bool contentThreads = false);
        // Call after enable + sealForSharedProcessors, on BSP kernel CR3, IF=0.
        bool Activate(TaskManager&, GlobalDescriptorTable&, memory::KernelPaging&,
                      memory::PhysicalMemoryManager&, NativeFpPolicy = NativeFpDisabled,
                      bool enableThreads = false);
        bool Create(const NativeImage&, uint32_t& id);
        // Validated static ET_EXEC only. The trusted source is consumed/copied
        // synchronously and is not retained. Stack+guards cannot overlap loads.
        bool CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id);
        bool CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id,
                       const NativeEnvironment& environment);
        // Kernel-selected large-image profile; ordinary callers retain256pages
        // and2MiB file limits. Runtime VM transactions retain the old256 cap.
        bool CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id,
                       uint32_t imagePageLimit);
        // Explicit root-stack/runtime budget. Stack2..128pages, runtime1..1024;
        // actual image+stack+runtime must fit the supported ownership capacity.
        // Both guards remain unmapped and excluded from runtime reservations.
        bool CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id,
                       const NativeElfLaunchProfile& profile);
        bool CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id,
                       const NativeElfLaunchProfile& profile, const NativeEnvironment& environment);
        // Trusted Content boot path with an independently bounded resident budget.
        bool CreateContentElf(const uint8_t* image, uint32_t bytes, uint32_t& id,
                              const NativeElfLaunchProfile& profile, const NativeEnvironment& environment);
        // Kernel-selected stable file. Close is called once even on failure and
        // must succeed before a loaded process becomes runnable.
        bool CreateContentElfFile(Elf32ReadAt read, bool (*close)(void*), void* source,
                                  uint32_t bytes, uint32_t& id, const NativeElfLaunchProfile&,
                                  const NativeEnvironment&);
        // Boot/desktop context only, never from a trap or on a victim stack.
        uint32_t Reap();
        bool RequestExit(uint32_t id, uint32_t code);
        bool Status(uint32_t id, NativeStatus&) const;
        // Minimal read-only observation of where an object actually lives in the slot table.
        //
        // A count of live objects cannot tell "the slot a reaped object vacated was reused" from "some other slot was
        // taken", and a diagnostic that cannot tell those apart has not shown slot reuse -- it has shown that a create
        // succeeded. This reports the slot index, the id the slot currently holds and the directory it is bound to, so a
        // caller can compare the slot before and after a reap and see the same index holding a different generation.
        // It returns no pointer, mutates nothing, takes no lock of its own beyond the interrupt guard the other readers
        // take, and is not part of any ABI or SDK surface.
        struct SlotObservation {
            uint32_t index, id, directory;
            bool occupied, live, reaped;
        };
        bool ObserveSlot(uint32_t id, SlotObservation&) const;
        uint32_t OccupiedSlots() const;
        bool MemoryStatistics(uint32_t id, memory::ProcessMemoryStatistics&) const;
        bool ReadMemory(uint32_t id, uint32_t address, void* out, uint32_t bytes) const;
        NativeStatistics Statistics() const;
        static NativeRuntime* Active();
        NativeFpError FpError() const { return fp.Error(); }
        NativeFpStatistics FpStatistics() const;
        bool FpEnabled() const { return fp.Enabled(); }
        // Internal assembly boundary hooks; caller holds IF=0.
        void EnterTrap(CPUState*);
        NativeFpTransition* PrepareTrapReturn(CPUState*);
        CPUState* HandleSyscall(CPUState*);
        // Null means not a supported verified user-origin fault: caller panics.
        CPUState* HandleFault(CPUState*, uint32_t address);
    };
} }
#endif

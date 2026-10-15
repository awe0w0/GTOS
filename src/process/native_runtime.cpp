#include <process/native_runtime.h>
#include <process/resources.h>
#include <process/native_surface.h>
#include <process/vm_abi.h>
#include <process/readonly_abi.h>
#include <process/clock_abi.h>
#include <process/info_abi.h>
#include <process/file_abi.h>
#include <process/elf32.h>
#include <process/fault_policy.h>
#include <memory/criticalsection.h>
using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;
void printf(char*);
void printfHex32(uint32_t);
namespace {
    int VmError(ProcessMemoryError error) {
        switch (error) {
        case ProcessMemoryNoMemory: return GTOS_VM_ERR_NO_MEMORY;
        case ProcessMemoryLimit:
        case ProcessMemoryRegionLimit: return GTOS_VM_ERR_LIMIT;
        case ProcessMemoryHandleExhausted: return GTOS_VM_ERR_HANDLE_EXHAUSTED;
        case ProcessMemoryBadRange: return GTOS_VM_ERR_RANGE;
        case ProcessMemoryConflict: return GTOS_VM_ERR_CONFLICT;
        case ProcessMemoryPermission: return GTOS_VM_ERR_PERMISSION;
        default: return GTOS_VM_ERR_BAD_STATE;
        }
    }
    bool BootstrapProcessor() {
        uint32_t before, after;
        asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%1;"
                     "pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(before), "=&r"(after) : : "cc", "memory");
        if (!((before ^ after) & 0x200000)) return false;
        uint32_t a, b, c, d;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
        if (a < 1) return false;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        if ((d & 0x220) != 0x220) return false; // APIC and MSR required for identity.
        asm volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0x1B));
        return (a & 0x100) != 0;
    }
    bool DisableFastEntry() {
        uint32_t a, b, c, d;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
        const bool intel = b == 0x756E6547U && d == 0x49656E69U && c == 0x6C65746EU;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        if (!(d & (1U << 5))) return false;
        if (d & (1U << 11)) {
            // Early Pentium Pro/II advertised SEP without usable fast entry.
            // Refuse ambiguous old Intel signatures rather than touching them.
            const uint32_t family = (a >> 8) & 15;
            const uint32_t model = ((a >> 4) & 15) | ((a >> 12) & 0xF0);
            if (intel && family == 6 && (model < 3 || (model == 3 && (a & 15) < 3)))
                return false;
            asm volatile("wrmsr" : : "c"(0x174), "a"(0), "d"(0) : "memory");
            asm volatile("wrmsr" : : "c"(0x175), "a"(0), "d"(0) : "memory");
            asm volatile("wrmsr" : : "c"(0x176), "a"(0), "d"(0) : "memory");
            for (uint32_t msr = 0x174; msr <= 0x176; ++msr) {
                asm volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(msr));
                if (a || d) return false;
            }
        }
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000U), "c"(0));
        if (a >= 0x80000001U) {
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001U), "c"(0));
            if (d & (1U << 11)) {
                asm volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xC0000080U));
                a &= ~1U; // Preserve every EFER bit except SCE.
                asm volatile("wrmsr" : : "c"(0xC0000080U), "a"(a), "d"(d) : "memory");
                asm volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xC0000080U));
                if (a & 1U) return false;
            }
        }
        return true;
    }
    uint32_t Cr3() { uint32_t value; asm volatile("mov %%cr3,%0" : "=r"(value)); return value; }
    void Zero(void* memory, uint32_t size) {
        uint8_t* bytes = (uint8_t*)memory;
        for (uint32_t i = 0; i < size; ++i) bytes[i] = 0;
    }
    uint32_t StackTop(uint32_t index) {
        return NativeRuntime::KernelStackArena + (index + 1) *
            (NativeRuntime::KernelStackPages + 1) * 4096;
    }
}
NativeRuntime* NativeRuntime::active = 0;
NativeRuntime::NativeRuntime() : paging(0), frames(0), scheduler(0), gdt(0),
    stacksPrepared(false), enabled(false), threadsEnabled(false), nextId(1), files(0) {
    Zero(&statistics, sizeof(statistics));
    for (uint32_t i = 0; i < MaximumProcesses; ++i) {
        Zero(&slots[i].status, sizeof(NativeStatus));
        NativeFpScrub(&slots[i].fp, sizeof(slots[i].fp));
        Zero(slots[i].stackFrames, sizeof(slots[i].stackFrames));
    }
}
bool NativeRuntime::AttachFiles(NativeFileEndpoint& endpoint) {
    InterruptGuard guard;
    if (enabled || files) return false;
    files = &endpoint;
    return true;
}
bool NativeRuntime::PrepareStacks(KernelPaging& kernel, PhysicalMemoryManager& allocator) {
    InterruptGuard guard;
    PagingStatistics info = kernel.getStatistics();
    if (!BootstrapProcessor() || !kernel.usesAllocator(allocator) || stacksPrepared || enabled
        || !info.prepared || info.enabled || info.sealedForSharing)
        return false;
    // Include every leading/inter-slot/trailing guard in collision checks.
    PagingMapping mapping;
    const uint32_t total = MaximumProcesses * (KernelStackPages + 1) + 1;
    for (uint32_t i = 0; i < total; ++i)
        if (kernel.query(KernelStackArena + i * 4096, mapping)) return false;
    uint32_t mapped = 0;
    for (uint32_t i = 0; i < MaximumProcesses; ++i) {
        for (uint32_t j = 0; j < KernelStackPages; ++j) {
            uint32_t& frame = slots[i].stackFrames[j];
            const uint32_t address = StackTop(i) - (KernelStackPages - j) * 4096;
            if (!allocator.allocate(frame)) goto rollback;
            Zero((void*)frame, 4096);
            if (!kernel.mapOwnedPage(address, frame, true)) {
                allocator.free(frame); frame = 0; goto rollback;
            }
            ++mapped;
        }
    }
    paging = &kernel; frames = &allocator; stacksPrepared = true;
    return true;
rollback:
    for (uint32_t n = 0; n < mapped; ++n) {
        const uint32_t i = n / KernelStackPages, j = n % KernelStackPages;
        kernel.unmapOwnedPage(StackTop(i) - (KernelStackPages - j) * 4096);
        allocator.free(slots[i].stackFrames[j]); slots[i].stackFrames[j] = 0;
    }
    // KernelPaging retains any newly created empty supervisor page table; it
    // still owns that frame and will reclaim it on pre-enable abandon().
    return false;
}
bool NativeRuntime::Activate(TaskManager& tasks, GlobalDescriptorTable& descriptors,
                             KernelPaging& kernel, PhysicalMemoryManager& allocator, NativeFpPolicy policy,
                             bool enableThreads) {
    uint32_t flags; asm volatile("pushfl; popl %0" : "=r"(flags));
    PagingStatistics info = kernel.getStatistics();
    if (!BootstrapProcessor() || (flags & 0x200) || active || enabled || !stacksPrepared || paging != &kernel
        || frames != &allocator || !kernel.usesAllocator(allocator) || !info.enabled || !info.sealedForSharing
        || Cr3() != info.directoryAddress || tasks.CurrentTask()) return false;
    if (!DisableFastEntry()) return false;
    NativeFpRecord* records[MaximumProcesses];
    for (uint32_t i = 0; i < MaximumProcesses; ++i) records[i] = &slots[i].fp;
    if (!fp.PrepareBsp(policy, kernel, records, MaximumProcesses)) return false;
    scheduler = &tasks; gdt = &descriptors;
    tasks.nativeGdt = gdt; tasks.kernelDirectory = info.directoryAddress;
    tasks.nativeFpEnabled = fp.Enabled();
    asm volatile("mov %%cr0,%0" : "=r"(tasks.kernelCr0));
    descriptors.LoadTaskState(StackTop(0));
    GtosClockReadResult clock = {};
    NativeRtcSnapshot calendar;
    if (scheduler->ReadClock(clock) == 0 && ReadNativeRtc(calendar))
        realtime.Initialize(calendar, clock);
    threadsEnabled = enableThreads; enabled = true; active = this;
    return true;
}
bool NativeRuntime::SafeKernelContext() const {
    return BootstrapProcessor() && enabled && scheduler && !scheduler->CurrentTask()
        && Cr3() == paging->getStatistics().directoryAddress;
}
NativeRuntime::Slot* NativeRuntime::FreeSlot(uint32_t& index) {
    for (index = 0; index < MaximumProcesses; ++index)
        if (!slots[index].occupied && !slots[index].space.Prepared()) return &slots[index];
    return 0;
}
bool NativeRuntime::Admit(Slot& slot, uint32_t index, uint32_t entry, uint32_t& id) {
    if (!slot.space.MapNewPage(UserStackBottom, true)
        || !slot.space.MapNewPage(UserStackBottom + 4096, true)
        || !slot.space.Seal()) return false;
    const uint32_t top = StackTop(index);
    Zero((void*)(top - KernelStackPages * 4096), KernelStackPages * 4096);
    slot.task.Initialize(gdt->CodeSegmentSelector(), 0);
    slot.task.cpustate = (CPUState*)(top - sizeof(CPUState));
    Zero(slot.task.cpustate, sizeof(CPUState));
    CPUState& cpu = *slot.task.cpustate;
    cpu.cs = gdt->UserCodeSegmentSelector();
    cpu.ds = cpu.es = cpu.fs = cpu.gs = cpu.ss = gdt->UserDataSegmentSelector();
    cpu.eip = entry;
    cpu.esp = UserStackTop - 16; cpu.eflags = 0x202; // IF=1, IOPL=NT=VM=0.
    slot.task.userMode = true; slot.task.directoryAddress = slot.space.DirectoryAddress();
    slot.task.kernelStackTop = top;
    if (!fp.Initialize(slot.fp, nextId)) return false;
    if (!scheduler->AddTask(&slot.task)) { fp.Invalidate(slot.fp); return false; }
    Zero(&slot.status, sizeof(slot.status));
    id = slot.status.id = nextId++; slot.status.directory = slot.space.DirectoryAddress();
    slot.status.live = slot.occupied = true; ++statistics.created;
    slot.process = &slot; slot.stackHandle = 0;
    slot.userStackBegin = UserStackBottom; slot.userStackEnd = UserStackTop;
    return true;
}
bool NativeRuntime::Create(const NativeImage& image, uint32_t& id) {
    InterruptGuard guard;
    id = 0;
    if (!SafeKernelContext() || !nextId || !image.code || !image.codeBytes || image.codeBytes > 4096
        || image.entryOffset >= image.codeBytes || image.dataBytes > 4096
        || (image.dataBytes && !image.data)) return false;
    uint32_t index; Slot* target = FreeSlot(index);
    if (!target || !target->space.Prepare(*paging, *frames)) return false;
    Slot& slot = *target;
    if (!slot.space.MapNewPage(CodeAddress, true)
        || !slot.space.MapNewPage(DataAddress, true)
        || !slot.space.CopyToUser(CodeAddress, image.code, image.codeBytes)
        || (image.dataBytes && !slot.space.CopyToUser(DataAddress, image.data, image.dataBytes))
        || !slot.space.ProtectPage(CodeAddress, false)
        || !Admit(slot, index, CodeAddress + image.entryOffset, id)) {
        slot.space.Destroy(); return false;
    }
    slot.codeCount = 1; slot.code[0].begin = CodeAddress;
    slot.code[0].end = CodeAddress + image.codeBytes;
    slot.dataCount = 1; slot.data[0].begin = DataAddress;
    slot.data[0].end = DataAddress + 4096;
    return true;
}
bool NativeRuntime::TrustedImage(const uint8_t* image, uint32_t bytes, uint32_t maximumBytes) const {
    const uint32_t address = (uint32_t)image;
    if (!address || !bytes || bytes > maximumBytes
        || (uint64_t)address + bytes > 0x100000000ULL) return false;
    const uint64_t end = (uint64_t)address + bytes;
    for (uint64_t page = address & ~4095U; page < end; page += 4096) {
        PagingMapping mapping;
        if (!paging->query((uint32_t)page, mapping) || mapping.userAccessible || mapping.cacheDisabled)
            return false;
    }
    return true;
}
bool NativeRuntime::CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id) {
    return CreateElf(image, bytes, id, Elf32MaximumPages);
}
bool NativeRuntime::CreateElf(const uint8_t* image, uint32_t bytes, uint32_t& id,
                              uint32_t imagePageLimit) {
    InterruptGuard guard;
    id = 0;
    if (!imagePageLimit || imagePageLimit > Elf32MaximumLargePages) return false;
    const bool large = imagePageLimit > Elf32MaximumPages;
    const uint32_t fileLimit = large ? Elf32MaximumLargePages * 4096 : 2 * 1024 * 1024;
    if (!SafeKernelContext() || !nextId || !TrustedImage(image, bytes, fileLimit)) return false;
    Elf32LoadPlan plan;
    Elf32Error validation;
    if (!ValidateElf32(image, bytes, plan, validation, imagePageLimit)) return false;
    const uint32_t capacity = large ? plan.pageCount + 2 + ProcessAddressSpace::MaximumPages
                                    : ProcessAddressSpace::MaximumPages;
    if (capacity > ProcessAddressSpace::MaximumCapacityPages || plan.pageCount > capacity - 2)
        return false;
    const uint32_t reservedBegin = UserStackBottom - 4096, reservedEnd = UserStackTop + 4096;
    for (uint32_t i = 0; i < plan.segmentCount; ++i) {
        const Elf32LoadSegment& segment = plan.segments[i];
        const uint32_t begin = segment.virtualAddress & ~4095U;
        const uint32_t end = (segment.virtualAddress + segment.memorySize + 4095) & ~4095U;
        if (begin < reservedEnd && reservedBegin < end) return false;
    }
    uint32_t index; Slot* target = FreeSlot(index);
    if (!target || !target->space.Prepare(*paging, *frames, capacity)) return false;
    Slot& slot = *target;
    for (uint32_t i = 0; i < plan.segmentCount; ++i) {
        const Elf32LoadSegment& segment = plan.segments[i];
        const uint32_t begin = segment.virtualAddress & ~4095U;
        const uint32_t end = (segment.virtualAddress + segment.memorySize + 4095) & ~4095U;
        if (!slot.space.MapNewRange(begin, end - begin, true)) goto rollback;
        if (segment.fileSize && !slot.space.CopyToUser(segment.virtualAddress,
                image + segment.fileOffset, segment.fileSize)) goto rollback;
        if (!(segment.flags & Elf32Write) && !slot.space.ProtectRange(begin, end - begin, false))
            goto rollback;
    }
    if (Admit(slot, index, plan.entry, id)) {
        slot.codeCount = slot.dataCount = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            if (segment.flags & Elf32Execute) {
                slot.code[slot.codeCount].begin = segment.virtualAddress;
                slot.code[slot.codeCount++].end = segment.virtualAddress + segment.memorySize;
            } else {
                slot.data[slot.dataCount].begin = segment.virtualAddress & ~4095U;
                slot.data[slot.dataCount++].end =
                    (segment.virtualAddress + segment.memorySize + 4095) & ~4095U;
            }
        }
        return true;
    }
rollback:
    slot.space.Destroy(); return false;
}
bool NativeRuntime::MemoryStatistics(uint32_t id, ProcessMemoryStatistics& result) const {
    InterruptGuard guard;
    if (!SafeKernelContext()) return false;
    for (uint32_t i = 0; i < MaximumProcesses; ++i)
        if (slots[i].occupied && slots[i].status.id == id && !slots[i].status.reaped)
            return slots[i].Space().QueryStatistics(result);
    return false;
}
NativeRuntime::Slot* NativeRuntime::Current() {
    if (!enabled) return 0;
    Task* task = scheduler->CurrentTask();
    if (!task || !task->UserMode()) return 0;
    for (uint32_t i = 0; i < MaximumProcesses; ++i)
        if (slots[i].occupied && &slots[i].task == task
            && Cr3() == slots[i].Space().DirectoryAddress()) return &slots[i];
    return 0;
}
void NativeRuntime::Observe(Slot& slot, const CPUState& cpu) {
    slot.status.observedCs = cpu.cs; slot.status.observedCr3 = Cr3();
    slot.status.observedEflags = cpu.eflags;
}
CPUState* NativeRuntime::Stop(Slot& slot, CPUState* cpu, uint32_t code) {
    StopGroup(*slot.process, code);
    return scheduler->Reschedule(cpu);
}
void NativeRuntime::StopGroup(Slot& process, uint32_t code) {
    NativeSurfaceBank::Instance().ReclaimOwner(process.status.id);
    for (uint32_t i = 0; i < MaximumProcesses; ++i) {
        Slot& member = slots[i];
        if (!member.occupied || member.process != &process || !member.status.live) continue;
        member.status.live = false; member.status.exitCode = code;
        scheduler->TerminateTask(&member.task);
    }
}
bool NativeRuntime::StackPinned(const Slot& process, uint32_t handle) const {
    if (!handle) return false;
    for (uint32_t i = 0; i < MaximumProcesses; ++i)
        if (slots[i].occupied && slots[i].process == &process && slots[i].stackHandle == handle)
            return true;
    return false;
}
bool NativeRuntime::ExecutableEntry(const Slot& process, uint32_t entry) const {
    for (uint32_t i = 0; i < process.codeCount; ++i)
        if (entry >= process.code[i].begin && entry < process.code[i].end) return true;
    return false;
}
bool NativeRuntime::LoadedDataRange(const Slot& process, uint32_t address, uint32_t bytes) const {
    if (bytes / 4096 > ProcessAddressSpace::MaximumPages) return false;
    const uint32_t end = address + bytes;
    for (uint32_t page = address; page < end; page += 4096) {
        bool found = false;
        for (uint32_t i = 0; i < process.dataCount; ++i)
            if (page >= process.data[i].begin && page + 4096 <= process.data[i].end) {
                found = true; break;
            }
        if (!found) return false;
    }
    return true;
}
int NativeRuntime::CreateThread(Slot& caller, const GtosThreadCreateRequest& request) {
    Slot& process = *caller.process;
    ProcessAddressSpace& space = process.space;
    if (request.version != GTOS_THREAD_ABI_VERSION) return GTOS_THREAD_ERR_UNSUPPORTED;
    if (request.flags) return GTOS_THREAD_ERR_BAD_SIZE;
    if (!space.ValidateUserRange(request.result_va, sizeof(uint32_t), true))
        return GTOS_THREAD_ERR_BAD_ADDRESS;
    if (!ExecutableEntry(process, request.entry)
        || !space.ValidateUserRange(request.entry, 1, false)) return GTOS_THREAD_ERR_BAD_ADDRESS;
    ProcessMemoryRegionInfo region;
    if (!space.QueryRegion(request.stack_handle, region)) return GTOS_THREAD_ERR_BAD_STATE;
    if (region.bytes < 4 * 4096 || region.bytes > 64 * 4096
        || (region.base & 4095) || (region.bytes & 4095)) return GTOS_THREAD_ERR_BAD_STATE;
    const uint32_t begin = region.base + 4096, end = region.base + region.bytes - 4096;
    // Both guards must be wholly inaccessible, even to reads. Region bounds
    // are validated by QueryRegion, so these additions cannot wrap.
    if (space.ValidateUserRange(region.base, 4096, false)
        || space.ValidateUserRange(end, 4096, false)) return GTOS_THREAD_ERR_BAD_STATE;
    if (!space.ValidateUserRange(begin, end - begin, true)) return GTOS_THREAD_ERR_BAD_ADDRESS;
    if ((uint64_t)request.result_va < (uint64_t)region.base + region.bytes
        && (uint64_t)region.base < (uint64_t)request.result_va + sizeof(uint32_t))
        return GTOS_THREAD_ERR_BAD_ADDRESS;
    if (StackPinned(process, request.stack_handle)) return GTOS_THREAD_ERR_BUSY;
    uint32_t index; Slot* target = FreeSlot(index);
    if (!target || !nextId || nextId > 0x7FFFFFFFU) return GTOS_THREAD_ERR_LIMIT;
    const uint32_t esp = end - 20;
    uint32_t previous[2], initial[2] = {0, request.argument};
    if (!space.CopyFromUser(previous, esp, sizeof(previous))) return GTOS_THREAD_ERR_BAD_ADDRESS;
    const uint32_t top = StackTop(index);
    Zero((void*)(top - KernelStackPages * 4096), KernelStackPages * 4096);
    target->task.Initialize(gdt->CodeSegmentSelector(), 0);
    target->task.cpustate = (CPUState*)(top - sizeof(CPUState));
    Zero(target->task.cpustate, sizeof(CPUState));
    CPUState& cpu = *target->task.cpustate;
    cpu.cs = gdt->UserCodeSegmentSelector();
    cpu.ds = cpu.es = cpu.fs = cpu.gs = cpu.ss = gdt->UserDataSegmentSelector();
    cpu.eip = request.entry; cpu.esp = esp; cpu.eflags = 0x202;
    target->task.userMode = true; target->task.directoryAddress = space.DirectoryAddress();
    target->task.kernelStackTop = top;
    if (!fp.Initialize(target->fp, nextId)) return GTOS_THREAD_ERR_BAD_STATE;
    if (!scheduler->AddTask(&target->task)) {
        fp.Invalidate(target->fp); return GTOS_THREAD_ERR_LIMIT;
    }
    // IF=0 on BSP: the admitted task cannot run before all checked copies
    // finish. On failure restore stack bytes, remove its frame and FP owner.
    if (!space.CopyToUser(esp, initial, sizeof(initial))
        || !space.CopyToUser(request.result_va, &nextId, sizeof(nextId))) {
        space.CopyToUser(esp, previous, sizeof(previous));
        scheduler->RemoveTask(&target->task); fp.Invalidate(target->fp);
        return GTOS_THREAD_ERR_BAD_ADDRESS;
    }
    Zero(&target->status, sizeof(target->status));
    target->process = &process; target->stackHandle = request.stack_handle;
    target->userStackBegin = begin; target->userStackEnd = end;
    target->status.id = nextId++; target->status.directory = space.DirectoryAddress();
    target->status.live = target->occupied = true; ++statistics.created;
    return 0;
}
int NativeRuntime::TryJoin(Slot& caller, const GtosThreadJoinRequest& request) {
    if (request.version != GTOS_THREAD_ABI_VERSION) return GTOS_THREAD_ERR_UNSUPPORTED;
    ProcessAddressSpace& space = caller.Space();
    if (!space.ValidateUserRange(request.result_va, sizeof(uint32_t), true))
        return GTOS_THREAD_ERR_BAD_ADDRESS;
    if (request.thread_id == caller.status.id) return GTOS_THREAD_ERR_DEADLOCK;
    for (uint32_t i = 0; i < MaximumProcesses; ++i) {
        Slot& target = slots[i];
        if (!target.occupied || !request.thread_id || target.status.id != request.thread_id) continue;
        if (target.process != caller.process || target.process == &target)
            return GTOS_THREAD_ERR_NO_THREAD;
        if (target.status.live) return GTOS_THREAD_ERR_BUSY;
        if (target.task.State() != TaskTerminated) return GTOS_THREAD_ERR_BAD_STATE;
        target.status.statistics = target.task.Statistics();
        if (target.task.owner && !scheduler->RemoveTask(&target.task)) return GTOS_THREAD_ERR_BAD_STATE;
        if (target.fp.initialized) fp.Invalidate(target.fp);
        if (!space.CopyToUser(request.result_va, &target.status.exitCode, sizeof(uint32_t)))
            return GTOS_THREAD_ERR_BAD_ADDRESS;
        target.status.reaped = true; target.occupied = false; target.stackHandle = 0;
        target.process = &target; ++statistics.reaped;
        return 0;
    }
    return GTOS_THREAD_ERR_NO_THREAD;
}
CPUState* NativeRuntime::HandleSyscall(CPUState* cpu) {
    Slot* thread = Current();
    if (!cpu || (cpu->cs & 3) != 3 || !thread) return 0;
    Slot* slot = thread->process;
    Observe(*thread, *cpu); ++thread->status.systemCalls;
    cpu->eflags = (cpu->eflags & 0xCD5U) | 0x202U;
    switch (cpu->eax) {
    case GTOS_SYS_FILE_OPEN:
    case GTOS_SYS_FILE_READ:
    case GTOS_SYS_FILE_WRITE:
    case GTOS_SYS_FILE_SEEK:
    case GTOS_SYS_FILE_CLOSE:
    case GTOS_SYS_FILE_SYNC:
    case GTOS_SYS_FILE_TRUNCATE:
    case GTOS_SYS_FILE_STAT:
    case GTOS_SYS_FILE_MKDIR:
    case GTOS_SYS_FILE_REMOVE:
    case GTOS_SYS_FILE_RENAME:
    case GTOS_SYS_FILE_DIR_OPEN:
    case GTOS_SYS_FILE_DIR_READ:
    case GTOS_SYS_FILE_DIR_REWIND:
    case GTOS_SYS_FILE_SIZE:
    case GTOS_SYS_FILE_HANDLE_INFO:
        cpu->eax = files ? (uint32_t)files->Call(slot->status.id, slot->space, cpu->eax, cpu->ebx, cpu->ecx)
            : (uint32_t)GTOS_FILE_ERR_UNAVAILABLE;
        break;
    case GTOS_SYS_ABI: cpu->eax = GTOS_NATIVE_ABI_VERSION; break;
    case GTOS_SYS_TICKS: cpu->eax = scheduler->Ticks(); break;
    case GTOS_SYS_CLOCK_READ: {
        if (cpu->ecx != GTOS_CLOCK_READ_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_CLOCK_ERR_BAD_SIZE; break;
        }
        GtosClockReadRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_CLOCK_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_CLOCK_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_CLOCK_ERR_UNSUPPORTED; break;
        }
        if (request.flags || request.result_bytes != GTOS_CLOCK_READ_RESULT_BYTES) {
            cpu->eax = (uint32_t)GTOS_CLOCK_ERR_BAD_SIZE; break;
        }
        if (request.clock_id != GTOS_CLOCK_ID_MONOTONIC) {
            cpu->eax = (uint32_t)GTOS_CLOCK_ERR_UNSUPPORTED; break;
        }
        if (!slot->space.ValidateUserRange(request.result_va, sizeof(GtosClockReadResult), true)) {
            cpu->eax = (uint32_t)GTOS_CLOCK_ERR_BAD_ADDRESS; break;
        }
        GtosClockReadResult result;
        const int status = scheduler->ReadClock(result);
        if (status < 0) cpu->eax = (uint32_t)status;
        // With IF clear on BSP, the fully validated output mappings cannot
        // change. No output byte is written until request/snapshot succeed.
        else cpu->eax = slot->space.CopyToUser(request.result_va, &result, sizeof(result))
            ? 0 : (uint32_t)GTOS_CLOCK_ERR_BAD_ADDRESS;
        break;
    }
    case GTOS_SYS_REALTIME_READ: {
        if (cpu->ecx != GTOS_REALTIME_READ_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_REALTIME_ERR_BAD_SIZE; break;
        }
        GtosRealtimeReadRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_REALTIME_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_REALTIME_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_REALTIME_ERR_UNSUPPORTED; break;
        }
        if (request.flags || request.result_bytes != GTOS_REALTIME_READ_RESULT_BYTES) {
            cpu->eax = (uint32_t)GTOS_REALTIME_ERR_BAD_SIZE; break;
        }
        if (!slot->space.ValidateUserRange(request.result_va, sizeof(GtosRealtimeReadResult), true)) {
            cpu->eax = (uint32_t)GTOS_REALTIME_ERR_BAD_ADDRESS; break;
        }
        GtosClockReadResult clock;
        GtosRealtimeReadResult result;
        const int clockStatus = scheduler->ReadClock(clock);
        const int status = clockStatus < 0 ? clockStatus : realtime.Read(clock, result);
        if (status < 0) cpu->eax = (uint32_t)status;
        else cpu->eax = slot->space.CopyToUser(request.result_va, &result, sizeof(result))
            ? 0 : (uint32_t)GTOS_REALTIME_ERR_BAD_ADDRESS;
        break;
    }
    case GTOS_SYS_PROCESS_INFO: {
        if (cpu->ecx != GTOS_PROCESS_INFO_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_PROCESS_INFO_ERR_BAD_SIZE; break;
        }
        GtosProcessInfoRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_PROCESS_INFO_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_PROCESS_INFO_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_PROCESS_INFO_ERR_UNSUPPORTED; break;
        }
        if (request.flags || request.result_bytes != GTOS_PROCESS_INFO_RESULT_BYTES) {
            cpu->eax = (uint32_t)GTOS_PROCESS_INFO_ERR_BAD_SIZE; break;
        }
        if (!slot->space.ValidateUserRange(request.result_va, sizeof(GtosProcessInfoResult), true)) {
            cpu->eax = (uint32_t)GTOS_PROCESS_INFO_ERR_BAD_ADDRESS; break;
        }
        ProcessMemoryStatistics memory;
        if (!slot->space.QueryStatistics(memory)) {
            cpu->eax = (uint32_t)GTOS_PROCESS_INFO_ERR_BAD_ADDRESS; break;
        }
        const GtosProcessInfoResult result = {GTOS_PROCESS_INFO_ABI_VERSION,
            slot->status.id, thread->status.id, thread->userStackBegin, thread->userStackEnd,
            ProcessAddressSpace::UserBase, ProcessAddressSpace::UserLimit,
            GTOS_VM_PAGE_BYTES, memory.capacityPages,
            ProcessAddressSpace::MaximumRegions, threadsEnabled ? MaximumProcesses : 1, 1};
        // The request was consumed before any write. IF is clear on BSP, so
        // full output validation also covers aliases and mixed-page failures.
        cpu->eax = slot->space.CopyToUser(request.result_va, &result, sizeof(result))
            ? 0 : (uint32_t)GTOS_PROCESS_INFO_ERR_BAD_ADDRESS;
        break;
    }
    case GTOS_SYS_YIELD:
        cpu->eax = 0; ++thread->task.statistics.yields;
        return scheduler->Reschedule(cpu);
    case GTOS_SYS_EXIT:
        ++statistics.exited; return Stop(*thread, cpu, cpu->ebx);
    case GTOS_SYS_THREAD_EXIT:
        if (!threadsEnabled) { cpu->eax = (uint32_t)GTOS_THREAD_ERR_UNSUPPORTED; break; }
        if (thread == slot) { cpu->eax = (uint32_t)GTOS_THREAD_ERR_BAD_STATE; break; }
        thread->status.live = false; thread->status.exitCode = cpu->ebx;
        ++statistics.exited; scheduler->TerminateTask(&thread->task);
        return scheduler->Reschedule(cpu);
    case GTOS_SYS_THREAD_CREATE: {
        if (!threadsEnabled) { cpu->eax = (uint32_t)GTOS_THREAD_ERR_UNSUPPORTED; break; }
        if (cpu->ecx != GTOS_THREAD_CREATE_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_THREAD_ERR_BAD_SIZE; break;
        }
        GtosThreadCreateRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request)))
            cpu->eax = (uint32_t)GTOS_THREAD_ERR_BAD_ADDRESS;
        else cpu->eax = (uint32_t)CreateThread(*thread, request);
        break;
    }
    case GTOS_SYS_THREAD_TRY_JOIN: {
        if (!threadsEnabled) { cpu->eax = (uint32_t)GTOS_THREAD_ERR_UNSUPPORTED; break; }
        if (cpu->ecx != GTOS_THREAD_JOIN_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_THREAD_ERR_BAD_SIZE; break;
        }
        GtosThreadJoinRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request)))
            cpu->eax = (uint32_t)GTOS_THREAD_ERR_BAD_ADDRESS;
        else cpu->eax = (uint32_t)TryJoin(*thread, request);
        break;
    }
    case GTOS_SYS_WRITE: {
        const uint32_t bytes = cpu->ecx;
        if (bytes > GTOS_NATIVE_WRITE_LIMIT) cpu->eax = (uint32_t)GTOS_ERR_TOO_LARGE;
        else if (!slot->space.CopyFromUser(bounce, cpu->ebx, bytes))
            cpu->eax = (uint32_t)GTOS_ERR_BAD_ADDRESS;
        else {
            // Validate/copy the WHOLE buffer before the first visible byte.
            // printf's string interface suppresses NUL bytes intentionally.
            char text[2] = {0, 0};
            for (uint32_t i = 0; i < bytes; ++i) { text[0] = bounce[i]; printf(text); }
            statistics.writtenBytes += bytes; cpu->eax = bytes;
        }
        thread->status.lastWriteResult = cpu->eax;
        break;
    }
    case GTOS_SYS_RESOURCE_INFO: {
        GtosResourceInfo info;
        const int result = ResourceInfo(cpu->ebx, info);
        if (result < 0) cpu->eax = (uint32_t)result;
        else if (!slot->space.CopyToUser(cpu->ecx, &info, sizeof(info)))
            cpu->eax = (uint32_t)GTOS_RESOURCE_ERR_BAD_ADDRESS;
        else cpu->eax = 0;
        break;
    }
    case GTOS_SYS_RESOURCE_READ: {
        // Check the wire size before reading any user request byte. Snapshot
        // once, so an output overlapping the request cannot alter this call.
        if (cpu->ecx != GTOS_RESOURCE_READ_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_RESOURCE_ERR_BAD_SIZE;
            break;
        }
        GtosResourceReadRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_RESOURCE_ERR_BAD_ADDRESS;
            break;
        }
        ResourceReadPlan plan;
        const int result = ResourcePlan(request, cpu->ecx, plan);
        if (result < 0) cpu->eax = (uint32_t)result;
        // Also copy a zero-byte plan: checked-copy retains the existing arena
        // requirement for empty output and validates the whole actual range.
        else if (!slot->space.CopyToUser(request.destination, plan.source, plan.bytes))
            cpu->eax = (uint32_t)GTOS_RESOURCE_ERR_BAD_ADDRESS;
        else cpu->eax = (uint32_t)result;
        break;
    }
    case GTOS_SYS_SURFACE_BEGIN: {
        if (cpu->ecx != GTOS_SURFACE_BEGIN_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_SIZE;
            break;
        }
        GtosSurfaceBeginRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request)))
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_ADDRESS;
        else cpu->eax = (uint32_t)NativeSurfaceBank::Instance().Begin(slot->status.id, request, cpu->ecx);
        break;
    }
    case GTOS_SYS_SURFACE_WRITE: {
        if (cpu->ecx != GTOS_SURFACE_WRITE_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_SIZE;
            break;
        }
        GtosSurfaceWriteRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_ADDRESS;
            break;
        }
        NativeSurfaceBank& surfaces = NativeSurfaceBank::Instance();
        const int result = surfaces.ValidateWrite(slot->status.id, request, cpu->ecx);
        static_assert(GTOS_SURFACE_WRITE_LIMIT <= GTOS_NATIVE_WRITE_LIMIT, "Surface bounce capacity");
        if (result < 0) cpu->eax = (uint32_t)result;
        else if (!slot->space.CopyFromUser(bounce, request.source, request.length))
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_ADDRESS;
        // Recheck owner/state and validate the WHOLE copied chunk before any
        // mutation. No user pointer survives this synchronous call.
        else cpu->eax = (uint32_t)surfaces.Write(slot->status.id, request, cpu->ecx, bounce);
        break;
    }
    case GTOS_SYS_SURFACE_PRESENT:
    case GTOS_SYS_SURFACE_ABORT: {
        const bool present = cpu->eax == GTOS_SYS_SURFACE_PRESENT;
        if (cpu->ecx != GTOS_SURFACE_CONTROL_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_SIZE;
            break;
        }
        GtosSurfaceControlRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request)))
            cpu->eax = (uint32_t)GTOS_SURFACE_ERR_BAD_ADDRESS;
        else if (present)
            cpu->eax = (uint32_t)NativeSurfaceBank::Instance().Present(slot->status.id, request, cpu->ecx);
        else cpu->eax = (uint32_t)NativeSurfaceBank::Instance().Abort(slot->status.id, request, cpu->ecx);
        break;
    }
    case GTOS_SYS_VM_RESERVE: {
        if (cpu->ecx != GTOS_VM_RESERVE_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_SIZE; break;
        }
        GtosVmReserveRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_VM_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_UNSUPPORTED_VERSION; break;
        }
        if (!slot->space.ValidateUserRange(request.result, GTOS_VM_RESERVE_RESULT_BYTES, true)) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS; break;
        }
        uint32_t base = 0, handle = 0;
        if (!slot->space.Reserve(request.length, request.alignment, request.hint, base, handle))
            cpu->eax = (uint32_t)VmError(slot->space.GetLastError());
        else {
            const GtosVmReserveResult result = {GTOS_VM_ABI_VERSION, handle, base,
                request.length, GTOS_VM_PAGE_BYTES};
            // Reserve changes only region metadata. With IF clear the fully
            // validated output mappings remain unchanged throughout this copy.
            if (slot->space.CopyToUser(request.result, &result, sizeof(result))) cpu->eax = 0;
            else {
                slot->space.Release(handle);
                cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS;
            }
        }
        break;
    }
    case GTOS_SYS_DATA_READONLY: {
        if (cpu->ecx != GTOS_READONLY_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_READONLY_ERR_BAD_SIZE; break;
        }
        GtosDataReadonlyRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_READONLY_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_READONLY_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_READONLY_ERR_UNSUPPORTED_VERSION; break;
        }
        if (!request.length || ((request.address | request.length) & 4095)
            || request.address < ProcessAddressSpace::UserBase
            || request.address >= ProcessAddressSpace::UserLimit
            || request.length > ProcessAddressSpace::UserLimit - request.address) {
            cpu->eax = (uint32_t)GTOS_READONLY_ERR_RANGE; break;
        }
        if (!LoadedDataRange(*slot, request.address, request.length)) {
            cpu->eax = (uint32_t)GTOS_READONLY_ERR_PERMISSION; break;
        }
        // The complete request is already snapshotted, even if it lies inside
        // the range being sealed. There is no user output buffer to write back.
        const bool ok = slot->space.SetDataReadOnly(request.address, request.length);
        cpu->eax = ok ? 0 : slot->space.GetLastError() == ProcessMemoryNotMapped
            ? (uint32_t)GTOS_READONLY_ERR_BAD_ADDRESS
            : (uint32_t)VmError(slot->space.GetLastError());
        break;
    }
    case GTOS_SYS_VM_SET_PERMISSIONS:
    case GTOS_SYS_VM_DECOMMIT:
    case GTOS_SYS_VM_DISCARD: {
        const uint32_t call = cpu->eax;
        if (cpu->ecx != GTOS_VM_RANGE_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_SIZE; break;
        }
        GtosVmRangeRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_VM_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_UNSUPPORTED_VERSION; break;
        }
        if (call != GTOS_SYS_VM_SET_PERMISSIONS && request.protection) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_STATE; break;
        }
        if (StackPinned(*slot, request.handle)) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_CONFLICT; break;
        }
        const bool ok = call == GTOS_SYS_VM_SET_PERMISSIONS
            ? slot->space.SetPermissions(request.handle, request.offset, request.length, request.protection)
            : call == GTOS_SYS_VM_DECOMMIT
            ? slot->space.Decommit(request.handle, request.offset, request.length)
            : slot->space.Discard(request.handle, request.offset, request.length);
        cpu->eax = ok ? 0 : (uint32_t)VmError(slot->space.GetLastError());
        break;
    }
    case GTOS_SYS_VM_RELEASE:
    case GTOS_SYS_VM_TRIM: {
        const bool release = cpu->eax == GTOS_SYS_VM_RELEASE;
        if (cpu->ecx != GTOS_VM_CONTROL_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_SIZE; break;
        }
        GtosVmControlRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_VM_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_UNSUPPORTED_VERSION; break;
        }
        if (release && request.length) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_STATE; break;
        }
        if (StackPinned(*slot, request.handle)) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_CONFLICT; break;
        }
        const bool ok = release ? slot->space.Release(request.handle)
            : slot->space.Trim(request.handle, request.length);
        cpu->eax = ok ? 0 : (uint32_t)VmError(slot->space.GetLastError());
        break;
    }
    case GTOS_SYS_VM_QUERY: {
        if (cpu->ecx != GTOS_VM_QUERY_REQUEST_BYTES) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_SIZE; break;
        }
        GtosVmQueryRequest request;
        if (!slot->space.CopyFromUser(&request, cpu->ebx, sizeof(request))) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS; break;
        }
        if (request.version != GTOS_VM_ABI_VERSION) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_UNSUPPORTED_VERSION; break;
        }
        if (!slot->space.ValidateUserRange(request.result, GTOS_VM_REGION_INFO_BYTES, true)) {
            cpu->eax = (uint32_t)GTOS_VM_ERR_BAD_ADDRESS; break;
        }
        ProcessMemoryRegionInfo info;
        if (!slot->space.QueryRegion(request.handle, info))
            cpu->eax = (uint32_t)VmError(slot->space.GetLastError());
        else {
            const GtosVmRegionInfo result = {GTOS_VM_ABI_VERSION, request.handle,
                info.base, info.bytes, info.residentPages};
            cpu->eax = slot->space.CopyToUser(request.result, &result, sizeof(result))
                ? 0 : (uint32_t)GTOS_VM_ERR_BAD_ADDRESS;
        }
        break;
    }
    default: cpu->eax = (uint32_t)GTOS_ERR_UNSUPPORTED; break;
    }
    return cpu;
}
CPUState* NativeRuntime::HandleFault(CPUState* cpu, uint32_t address) {
    if (!cpu || !RecoverableUserFault(*cpu)) return 0;
    Slot* slot = Current(); if (!slot) return 0;
    Observe(*slot, *cpu); slot->status.faultVector = cpu->vector;
    slot->status.faultError = cpu->error; slot->status.faultAddress = address;
    ++statistics.faulted;
    printf("\nNATIVE USER FAULT id="); printfHex32(slot->status.id);
    printf(" vector="); printfHex32(cpu->vector); printf("\n");
    return Stop(*slot, cpu, 0x80000000U | cpu->vector);
}
uint32_t NativeRuntime::Reap() {
    InterruptGuard guard;
    if (!SafeKernelContext()) return 0;
    uint32_t reaped = 0;
    for (uint32_t i = 0; i < MaximumProcesses; ++i) {
        Slot& process = slots[i];
        if (!process.occupied || process.process != &process || process.status.live) continue;
        bool removed = true;
        for (uint32_t j = 0; j < MaximumProcesses; ++j) {
            Slot& member = slots[j];
            if (!member.occupied || member.process != &process) continue;
            if (member.status.live || member.task.State() != TaskTerminated) { removed = false; continue; }
            member.status.statistics = member.task.Statistics();
            if (member.task.owner && !scheduler->RemoveTask(&member.task)) { removed = false; continue; }
            // No task/frame may still reference the CR3 when Destroy runs.
            if (member.fp.initialized) fp.Invalidate(member.fp);
        }
        if (!removed) continue;
        NativeSurfaceBank::Instance().ReclaimOwner(process.status.id);
        if (files) files->ReclaimOwner(process.status.id);
        if (!process.space.Destroy()) continue;
        for (uint32_t j = 0; j < MaximumProcesses; ++j) {
            Slot& member = slots[j];
            if (!member.occupied || member.process != &process) continue;
            member.occupied = false; member.status.reaped = true; member.stackHandle = 0;
            member.process = &member; ++statistics.reaped; ++reaped;
        }
    }
    return reaped;
}
bool NativeRuntime::Status(uint32_t id, NativeStatus& result) const {
    InterruptGuard guard;
    for (uint32_t i = 0; i < MaximumProcesses; ++i) if (id && slots[i].status.id == id) {
        result = slots[i].status;
        if (slots[i].occupied) result.statistics = slots[i].task.Statistics();
        return true;
    }
    return false;
}
bool NativeRuntime::ReadMemory(uint32_t id, uint32_t address, void* out, uint32_t bytes) const {
    InterruptGuard guard;
    if (!SafeKernelContext()) return false;
    for (uint32_t i = 0; i < MaximumProcesses; ++i)
        if (slots[i].occupied && slots[i].status.id == id)
            return slots[i].Space().CopyFromUser(out, address, bytes);
    return false;
}
NativeStatistics NativeRuntime::Statistics() const { InterruptGuard guard; return statistics; }
NativeRuntime* NativeRuntime::Active() { return active; }

bool NativeRuntime::RequestExit(uint32_t id, uint32_t code) {
    InterruptGuard guard;
    if (!SafeKernelContext()) return false;
    for (uint32_t i = 0; i < MaximumProcesses; ++i) {
        Slot& slot = slots[i];
        if (id && slot.occupied && slot.status.id == id && slot.status.live) {
            ++statistics.exited;
            StopGroup(*slot.process, code);
            return true;
        }
    }
    return false;
}

void NativeRuntime::EnterTrap(CPUState* cpu) {
    if (!fp.Enabled()) return;
    if (!BootstrapProcessor() || !cpu) fp.Panic("entry CPU/frame");
    const bool user = (cpu->cs & 3) == 3;
    Slot* slot = user ? Current() : 0;
    if (user) {
        if (!slot || !slot->status.live) fp.Panic("entry current slot");
        const uint32_t top = slot->task.kernelStackTop, address = (uint32_t)cpu;
        if (address < top - KernelStackPages * 4096 || address > top - sizeof(CPUState))
            fp.Panic("entry frame range");
    }
    fp.EnterKernel(slot ? &slot->fp : 0, slot ? slot->status.id : 0, user, cpu->vector);
}
NativeFpTransition* NativeRuntime::PrepareTrapReturn(CPUState* cpu) {
    if (!fp.Enabled()) return 0;
    if (!BootstrapProcessor() || !cpu) fp.Panic("return CPU/frame");
    const bool user = (cpu->cs & 3) == 3;
    Slot* slot = user ? Current() : 0;
    if (user) {
        if (!slot || !slot->status.live || slot->task.State() != TaskRunning)
            fp.Panic("return current slot");
        const uint32_t top = slot->task.kernelStackTop, address = (uint32_t)cpu;
        if (address < top - KernelStackPages * 4096 || address > top - sizeof(CPUState)
            || cpu->cs != gdt->UserCodeSegmentSelector() || cpu->ss != gdt->UserDataSegmentSelector())
            fp.Panic("return frame range/selectors");
    }
    if (!user && ((scheduler->CurrentTask() && scheduler->CurrentTask()->UserMode())
        || Cr3() != paging->getStatistics().directoryAddress))
        fp.Panic("kernel return task/CR3");
    return fp.PrepareReturn(slot ? &slot->fp : 0, slot ? slot->status.id : 0, user);
}
extern "C" void native_fp_enter_trap(CPUState* cpu) {
    NativeRuntime* runtime = NativeRuntime::Active();
    if (runtime) runtime->EnterTrap(cpu);
}
extern "C" NativeFpTransition* native_fp_prepare_return(CPUState* cpu) {
    NativeRuntime* runtime = NativeRuntime::Active();
    return runtime ? runtime->PrepareTrapReturn(cpu) : 0;
}

NativeFpStatistics NativeRuntime::FpStatistics() const {
    InterruptGuard guard; return fp.Statistics();
}

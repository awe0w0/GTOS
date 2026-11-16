#define NativeProcessSmoke ContentSharedMemoryUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/memory_record.h"
#include <process/native_shared_memory.h>
#include <process/shared_memory_abi.h>

namespace {
    uint8_t image[65536];
    void FinishShared(bool pass) __attribute__((noreturn));
    void FinishShared(bool pass) {
        printf(pass ? (char*)"CONTENT SHARED MEMORY PASS\n" : (char*)"CONTENT SHARED MEMORY FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void RequireShared(bool value, const char* reason) {
        if (value) return;
        printf((char*)"CONTENT SHARED MEMORY REJECT ");
        printf((char*)reason);
        printf((char*)"\n");
        FinishShared(false);
    }

    template <typename Request>
    int32_t CallShared(NativeSharedMemory& shared, ProcessAddressSpace& space,
                       uint32_t owner, uint32_t operation, const Request& request) {
        const uint32_t address = ProcessAddressSpace::UserBase;
        RequireShared(space.CopyToUser(address, &request, sizeof(request)), "capacity copy request");
        return shared.Call(owner, space, operation, address, sizeof(request));
    }

    void TestSharedCapacity(KernelPaging& paging, PhysicalMemoryManager& frames, bool content) {
        const uint32_t baseline = frames.getStatistics().freeFrames;
        const uint32_t owner = 1;
        const uint32_t address = ProcessAddressSpace::UserBase;
        const uint32_t page = GTOS_SHARED_MEMORY_PAGE_BYTES;
        const uint32_t version = GTOS_SHARED_MEMORY_ABI_VERSION;
        const uint32_t kernelDirectory = paging.getStatistics().directoryAddress;
        const uint32_t objects = content ? NativeSharedMemory::ContentMaximumObjects
                                         : NativeSharedMemory::MaximumObjects;
        const uint32_t handles = content ? NativeSharedMemory::ContentMaximumHandles
                                         : NativeSharedMemory::MaximumHandles;
        const uint32_t mappings = content ? NativeSharedMemory::ContentMaximumMappings
                                          : NativeSharedMemory::MaximumMappings;
        NativeSharedMemory shared(frames);
        if (content) RequireShared(shared.EnableContentProfile(), "enable Content shared profile");
        ProcessAddressSpace space;
        // Isolate the endpoint bounds from the address-space region bound.
        uint32_t capacity = mappings + 1;
        if (capacity < ProcessAddressSpace::MaximumPages)
            capacity = ProcessAddressSpace::MaximumPages;
        RequireShared(space.PrepareContent(paging, frames, capacity, ProcessAddressSpace::DynamicLimit),
                      "prepare capacity address space");
        RequireShared(space.MapNewPage(address, true), "map capacity request page");
        RequireShared(space.Seal(), "seal capacity address space");
        const GtosSharedMemoryCreateRequest create{version, page, GTOS_SHARED_MEMORY_UNSAFE};
        uint32_t objectHandles[NativeSharedMemory::ContentMaximumObjects];
        for (uint32_t i = 0; i < objects; ++i) {
            const int32_t id = CallShared(shared, space, owner, GTOS_SYS_SHARED_MEMORY_CREATE, create);
            RequireShared(id > 0, "fill shared objects");
            objectHandles[i] = id;
        }
        RequireShared(CallShared(shared, space, owner, GTOS_SYS_SHARED_MEMORY_CREATE, create) ==
                          GTOS_SHARED_MEMORY_ERR_LIMIT,
                      "shared object cap");
        RequireShared(!shared.EnableContentProfile(), "refuse changing a live shared table");
        const GtosSharedMemoryControlRequest duplicate{version, objectHandles[0]};
        RequireShared(CallShared(shared, space, owner + 1, GTOS_SYS_SHARED_MEMORY_DUP, duplicate) ==
                          GTOS_SHARED_MEMORY_ERR_BAD_HANDLE,
                      "shared handle owner isolation");
        for (uint32_t i = objects; i < handles; ++i)
            RequireShared(CallShared(shared, space, owner, GTOS_SYS_SHARED_MEMORY_DUP, duplicate) > 0,
                          "fill shared handles");
        RequireShared(CallShared(shared, space, owner, GTOS_SYS_SHARED_MEMORY_DUP, duplicate) ==
                          GTOS_SHARED_MEMORY_ERR_LIMIT,
                      "shared handle cap");

        const uint32_t directory = space.DirectoryAddress();
        asm volatile("movl %0, %%cr3" : : "r"(directory) : "memory");
        GtosSharedMemoryMapRequest map{version, 0, 0, page, 0, address + 64};
        for (uint32_t i = 0; i < mappings; ++i) {
            map.handle = objectHandles[i % objects];
            RequireShared(CallShared(shared, space, owner, GTOS_SYS_SHARED_MEMORY_MAP, map) == 0,
                          "fill shared mappings");
        }
        RequireShared(CallShared(shared, space, owner, GTOS_SYS_SHARED_MEMORY_MAP, map) ==
                          GTOS_SHARED_MEMORY_ERR_LIMIT,
                      "shared mapping cap");
        asm volatile("movl %0, %%cr3" : : "r"(kernelDirectory) : "memory");
        RequireShared(space.Destroy(), "destroy capacity test address space");
        shared.ReclaimOwner(owner);
        RequireShared(frames.getStatistics().freeFrames == baseline, "capacity test frame accounting");
        printf(content ? (char*)"CONTENT SHARED EXTENDED CAPACITY PASS\n"
                       : (char*)"CONTENT SHARED DEFAULT CAPACITY PASS\n");
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    RequireShared(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    RequireShared(paging.prepareIdentity(frames, config), "identity paging");
    NativeSharedMemory shared(frames);
    NativeRuntime runtime;
    RequireShared(runtime.AttachSharedMemory(shared) && runtime.PrepareStacks(paging, frames), "endpoint and stacks");
    RequireShared(paging.enable() && paging.sealForSharedProcessors(), "sealed paging template");
    TestSharedCapacity(paging, frames, false);
    TestSharedCapacity(paging, frames, true);
    RequireShared(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native runtime");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    RequireShared(tasks.AddTask(&ring0), "kernel peer");
    const auto* boot = static_cast<const MultibootInfo*>(multiboot);
    RequireShared((boot->flags & 8) && boot->moduleCount == 2, "two supplied ELF modules");
    const auto* modules = reinterpret_cast<const MultibootModule*>(boot->modules);
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 2; ++mode) {
        const auto& module = modules[mode];
        RequireShared(module.end > module.start && module.end - module.start <= sizeof(image), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t i = 0; i < bytes; ++i) image[i] = ((const uint8_t*)module.start)[i];
        uint32_t id = 0;
        RequireShared(runtime.CreateElf(image, bytes, id), "ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_probe::Record record = {};
        RequireShared(runtime.ReadMemory(id, gtos_perfetto_probe::RecordAddress, &record, sizeof(record)) &&
            record.version == 1 && record.mode == mode, "consumer record");
        RequireShared(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress, "CPL3 isolation");
        RequireShared(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline,
            "Reap released tables and all backing frames");
        printf((char*)"CONTENT SHARED MEMORY RECORD mode="); printfHex32(mode);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" failures="); printfHex32(record.failures);
        printf((char*)" exit="); printfHex32(status.exitCode);
        printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" error="); printfHex32(status.faultError);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline);
        printf((char*)"\n");
        if (mode == 0) {
            RequireShared(record.stage == 2 && record.checks == 32 && !record.failures &&
                !status.exitCode && !status.faultVector, "alias semantics");
        } else {
            RequireShared(record.stage == 1 && record.checks == 2 && !record.failures &&
                record.fault_address >= ProcessAddressSpace::DynamicBase &&
                status.exitCode == 0x8000000EU && status.faultVector == 14 &&
                (status.faultError & 7) == 7 && status.faultAddress == record.fault_address,
                "write to read-only alias faulted");
        }
    }
    FinishShared(true);
}

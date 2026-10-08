// Reuse accepted boot helpers only; the original entry is discarded by GC.
#define NativeProcessSmoke NativeThreadIdUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include <memory/criticalsection.h>
#include "../apps/native_thread_id_probe/record.h"
#include "../apps/v8_thread_id_probe/emutls.h"

namespace {
    struct TlsExpectedControl {
        uint32_t control, size, alignment, initial, role, slot, patternTag;
    };
    struct TlsExpectedInput {
        uint32_t mode, nonce, count;
        TlsExpectedControl controls[GTOS_TLS_PROBE_MAX_OBJECTS];
    };
}
#include "thread_id_inputs.h"

namespace {
    uint8_t tlsElf[65536], tlsBytes[257];
    uint32_t kernelDirectory, survivorFrames, tlsPeerId;
    void TlsFinish(bool passed) __attribute__((noreturn));
    void TlsFinish(bool passed) {
        printf(passed ? (char*)"NATIVE THREAD ID SMOKE PASS\n" : (char*)"NATIVE THREAD ID SMOKE FAIL\n");
        asm volatile("outl %0,%1" : : "a"(passed ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void TlsRequire(bool value, const char* why) {
        if (!value) {
            InterruptGuard guard;
            printf((char*)"FAILED TLS "); printf((char*)why); printf((char*)"\n"); TlsFinish(false);
        }
    }
    uint32_t TlsStaticCost(const Elf32LoadPlan& plan, uint32_t* seen) {
        uint32_t tables = 0;
        for (uint32_t i = 0; i < 32; ++i) seen[i] = 0;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            for (uint32_t di = segment.virtualAddress >> 22;
                 di <= (segment.virtualAddress + segment.memorySize - 1) >> 22; ++di) {
                const uint32_t bit = 1U << (di & 31);
                if (!(seen[di >> 5] & bit)) { seen[di >> 5] |= bit; ++tables; }
            }
        }
        const uint32_t stack = NativeRuntime::UserStackBottom >> 22;
        if (!(seen[stack >> 5] & (1U << (stack & 31)))) { seen[stack >> 5] |= 1U << (stack & 31); ++tables; }
        return plan.pageCount + 2 + tables + 1;
    }
    uint32_t TlsCreate(NativeRuntime& runtime, const MultibootModule& module, const TlsExpectedInput& expected,
                       uint32_t& staticCost, uint32_t& dynamicCost) {
        const uint32_t bytes = module.end - module.start;
        TlsRequire(module.end > module.start && bytes <= sizeof(tlsElf), "bounded actual TLS ELF file");
        Elf32LoadPlan plan;
        TlsRequire(ValidateElf32((const uint8_t*)module.start, bytes, plan) && plan.pageCount + 2 + 16 <= 256,
            "actual ELF32 admission and full static/stack/heap resident budget");
        uint32_t seen[32]; staticCost = TlsStaticCost(plan, seen);
        // No earlier dynamic reservation: firstfit is 80000000, 16 pages within PDE512.
        const uint32_t heapDi = 0x80000000U >> 22;
        TlsRequire(!(seen[heapDi >> 5] & (1U << (heapDi & 31))), "heap PDE absent from actual static and stack geometry");
        dynamicCost = 16 + 1;
        for (uint32_t i = 0; i < bytes; ++i) tlsElf[i] = ((const uint8_t*)module.start)[i];
        bool annotated = false;
        for (uint32_t i = 0; i < plan.segmentCount; ++i) {
            const Elf32LoadSegment& segment = plan.segments[i];
            const uint32_t va = GTOS_TLS_PROBE_RECORD_VA;
            if (va < segment.virtualAddress || va - segment.virtualAddress > segment.fileSize
                || segment.fileSize - (va - segment.virtualAddress) < sizeof(NativeTlsProbeRecord)) continue;
            const uint32_t offset = segment.fileOffset + va - segment.virtualAddress;
            TlsRequire((segment.flags & Elf32Write) && !(segment.flags & Elf32Execute)
                && Get32(tlsElf + offset) == 1 && Get32(tlsElf + offset + 4) == sizeof(NativeTlsProbeRecord)
                && Get32(tlsElf + offset + 8) == 1 && Get32(tlsElf + offset + 12) == expected.mode
                && Get32(tlsElf + offset + 28) == GTOS_TLS_PROBE_VICTIM_NONCE,
                "actual initialized512B TLS record in writable non-executable data");
            Put32(tlsElf + offset + 28, expected.nonce); annotated = true;
        }
        TlsRequire(annotated, "only private load-copy nonce is annotated");
        uint32_t id = 0;
        TlsRequire(runtime.CreateElf(tlsElf, bytes, id) && id, "real TLS ELF admitted and runnable"); return id;
    }
    NativeTlsProbeRecord TlsRecord(NativeRuntime& runtime, uint32_t id) {
        NativeTlsProbeRecord record = {};
        TlsRequire(runtime.ReadMemory(id, GTOS_TLS_PROBE_RECORD_VA, &record, sizeof(record)), "read actual512B TLS record");
        return record;
    }
    void TlsRecordLog(const NativeTlsProbeRecord& record, uint32_t phase) {
        InterruptGuard guard;
        printf((char*)"TLS RECORD phase="); printfHex32(phase); printf((char*)" words=");
        const uint8_t* bytes = (const uint8_t*)&record;
        for (uint32_t i = 0; i < sizeof(record); i += 4) { printf((char*)" "); printfHex32(Get32(bytes + i)); }
        printf((char*)"\n");
    }
    NativeTlsProbeRecord TlsStage(NativeRuntime& runtime, TaskManager& tasks, uint32_t id, uint32_t stage) {
        const uint32_t begin = tasks.Ticks();
        for (;;) {
            NativeTlsProbeRecord record = TlsRecord(runtime, id);
            if (record.version == 1 && record.stage >= stage) {
                TlsRequire(record.stage == stage, "observe exact live phase before the consumer changes its storage");
                return record;
            }
            NativeStatus status = {};
            TlsRequire(runtime.Status(id, status) && status.live, "TLS consumer lives until required initialization phase");
            TlsRequire((uint32_t)(tasks.Ticks() - begin) < 1000, "bounded phase deadline without changing production TLS policy");
            WaitTicks(tasks, 1);
        }
    }
    void TlsIdentity(const NativeTlsProbeRecord& record, const TlsExpectedInput& input) {
        TlsRequire(record.version == 1 && record.bytes == sizeof(record) && record.kind == 1
            && record.mode == input.mode && record.nonce == input.nonce && record.object_count == input.count
            && record.checks && record.error == 0,
            "actual record identity bound to immutable genuine input");
        TlsRequire(record.heap_base == 0x80000000U && record.heap_handle && record.heap_handle <= 0x7FFFFFFFU
            && record.heap_length == 65536 && record.heap_resident_pages == 16 && record.first_handle == record.heap_handle,
            "actual heap query reports full16-page firstfit arena");
    }
    GtosEmutlsControl TlsControl(NativeRuntime& runtime, uint32_t id, const TlsExpectedControl& expected) {
        uint8_t bytes[GTOS_EMUTLS_CONTROL_BYTES];
        TlsRequire(runtime.ReadMemory(id, expected.control, bytes, sizeof(bytes)), "read actual compiler16B control in private PAS");
        const GtosEmutlsControl result = {Get32(bytes), Get32(bytes + 4), Get32(bytes + 8), Get32(bytes + 12)};
        return result;
    }
    void TlsControls(NativeRuntime& runtime, uint32_t id, const NativeTlsProbeRecord& record,
                     const TlsExpectedInput& expected, uint32_t phase) {
        InterruptGuard guard;
        for (uint32_t role = 0; role < expected.count; ++role) {
            const TlsExpectedControl& want = expected.controls[role];
            const NativeTlsProbeObject& object = record.objects[role];
            const GtosEmutlsControl control = TlsControl(runtime, id, want);
            const uint32_t size = phase == 3 && expected.mode == 6 && role == 4 ? 0xFFFFFFFFU : want.size;
            const uint32_t alignment = phase == 3 && expected.mode == 5 && role == 4 ? 6U : want.alignment;
            TlsRequire(control.size == size && control.alignment == alignment && control.initial_value == want.initial,
                "compiler metadata matches actual emitted inventory or the exact intended mutation");
            TlsRequire(object.control_va == want.control && object.size == want.size && object.alignment == want.alignment
                && object.template_va == want.initial, "record object metadata matches independently decoded actual compiler control");
            const bool retainedObject = phase == 1 ? role == 0 : role < 4;
            TlsRequire(want.role == role && object.byte_count == (retainedObject ? want.size : 0U)
                && object.flags == (role | (want.slot << 8) | (retainedObject ? 0x10000U : 0U)),
                "actual role, compiler slot and whole observed object size");
            if (phase != 1 && role > 0 && role < 4)
                TlsRequire(object.pattern_tag == ((expected.nonce ^ want.patternTag) & 255U),
                    "reported pattern tag agrees with independently defined owner/role pattern");
            const bool initialized = phase == 1 ? role == 0 : role < 4;
            const bool cleaned = phase == 3 && expected.mode == 0;
            if (!initialized || cleaned) TlsRequire(control.cached_address == 0, "uninitialized or finalized control publishes no address");
            else TlsRequire(control.cached_address == object.address && object.address >= record.heap_base
                && object.address - record.heap_base <= record.heap_length
                && want.size <= record.heap_length - (object.address - record.heap_base)
                && !(object.address & (want.alignment - 1U)), "actual initialized compiler cache and object extent/alignment");
            if (initialized && !cleaned) for (uint32_t previous = 0; previous < role; ++previous) {
                const NativeTlsProbeObject& other = record.objects[previous];
                TlsRequire(object.address >= other.address + other.size || other.address >= object.address + want.size,
                    "independently observed healthy TLS storage ranges never overlap");
            }
            {
                InterruptGuard guard;
                printf((char*)"TLS CONTROL phase="); printfHex32(phase); printf((char*)" role="); printfHex32(role);
                printf((char*)" va="); printfHex32(want.control); printf((char*)" size="); printfHex32(control.size);
                printf((char*)" alignment="); printfHex32(control.alignment); printf((char*)" cache="); printfHex32(control.cached_address);
                printf((char*)" initial="); printfHex32(control.initial_value); printf((char*)"\n");
            }
            if (cleaned && role < 4)
                TlsRequire(object.address && !runtime.ReadMemory(id, object.address, tlsBytes, 1), "normal finalizer releases old TLS mapping");
            else if (phase == 1 && role == 0) {
                TlsRequire(runtime.ReadMemory(id, object.address, tlsBytes, 4) && Get32(tlsBytes) == 0,
                    "first genuineTry allocated actual zero int before Current");
            } else if (phase != 1 && initialized) {
                TlsRequire(want.size <= sizeof(tlsBytes) && runtime.ReadMemory(id, object.address, tlsBytes, want.size),
                    "read whole actual healthy TLS object");
                for (uint32_t i = 0; i < want.size; ++i)
                    TlsRequire(tlsBytes[i] == (role == 0 ? (i == 0 ? 1U : 0U)
                        : (uint8_t)(i ^ expected.nonce ^ want.patternTag)), "whole TLS values remain owner-specific");
            }
        }
    }
    void TlsFirstTry(NativeRuntime& runtime, PhysicalMemoryManager& frames, uint32_t id,
                     NativeTlsProbeRecord& record, const TlsExpectedInput& expected,
                     uint32_t baseline, uint32_t staticCost, uint32_t dynamicCost) {
        InterruptGuard guard;
        record = TlsRecord(runtime, id);
        TlsRequire(record.stage == 1, "complete firstTry snapshot before Current");
        TlsRecordLog(record, 1);
        TlsIdentity(record, expected);
        TlsRequire(record.try_before == 0xFFFFFFFFU && record.initialized_count == 1
            && !record.current && baseline - frames.getStatistics().freeFrames == staticCost + dynamicCost,
            "first genuineTry Invalid still owns exact full arena before Current");
        TlsControls(runtime, id, record, expected, 1);
        printf((char*)"TLS FIRST_TRY mode="); printfHex32(expected.mode); printf((char*)" nonce="); printfHex32(expected.nonce);
        printf((char*)" static="); printfHex32(staticCost); printf((char*)" dynamic="); printfHex32(dynamicCost);
        printf((char*)" retained="); printfHex32(baseline - frames.getStatistics().freeFrames); printf((char*)" live=00000001\n");
    }
    void TlsInitialized(NativeRuntime& runtime, uint32_t id, NativeTlsProbeRecord& record, const TlsExpectedInput& expected) {
        InterruptGuard guard;
        record = TlsRecord(runtime, id);
        TlsRequire(record.stage == 2, "complete healthy snapshot before terminal transition");
        TlsRecordLog(record, 2);
        TlsIdentity(record, expected);
        TlsRequire(record.try_before == 0xFFFFFFFFU && record.current == 1 && record.try_after == 1 && record.repeated == 1
            && record.initialized_count == 4, "genuine ThreadId methods and four healthy controls");
        TlsControls(runtime, id, record, expected, 2);
    }
    void TlsSurvivors(NativeRuntime& runtime, TaskManager& tasks, uint32_t cpuPeer,
                      const NativeTlsProbeRecord& peerRecord, uint32_t beforeCpu, uint32_t beforeCpuRunTicks, uint32_t beforeYields,
                      uint32_t beforeRunTicks, uint32_t beforeRing0, uint32_t beforeBoot, uint32_t beforeTicks) {
        // Yield dispatches do not charge runTicks; the CPU-only peer proves IRQ preemption.
        WaitTicks(tasks, 8);
        uint32_t data[4]; NativeStatus cpu = {}, peer = {};
        TlsRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && runtime.Status(cpuPeer, cpu) && runtime.Status(tlsPeerId, peer), "actual surviving peer snapshots");
        const uint32_t afterRing0 = ring0Progress, afterBoot = tasks.BootTicks(), afterTicks = tasks.Ticks();
        {
            InterruptGuard guard;
            // Capture every component before any combined predicate can fail.
            printf((char*)"TLS SURVIVORS cpu_before="); printfHex32(beforeCpu); printf((char*)" cpu_after="); printfHex32(data[3]);
            printf((char*)" cpu_live="); printfHex32(cpu.live); printf((char*)" peer_live="); printfHex32(peer.live);
            printf((char*)" cpu_calls="); printfHex32(cpu.systemCalls); printf((char*)" cpu_yields="); printfHex32(cpu.statistics.yields);
            printf((char*)" cpu_run_before="); printfHex32(beforeCpuRunTicks); printf((char*)" cpu_run_after="); printfHex32(cpu.statistics.runTicks);
            printf((char*)" peer_yields_before="); printfHex32(beforeYields); printf((char*)" peer_yields_after="); printfHex32(peer.statistics.yields);
            printf((char*)" peer_run_before="); printfHex32(beforeRunTicks); printf((char*)" peer_run_after="); printfHex32(peer.statistics.runTicks);
            printf((char*)" ticks_before="); printfHex32(beforeTicks); printf((char*)" ticks_after="); printfHex32(afterTicks);
            printf((char*)" ring0_before="); printfHex32(beforeRing0); printf((char*)" ring0_after="); printfHex32(afterRing0);
            printf((char*)" boot_before="); printfHex32(beforeBoot); printf((char*)" boot_after="); printfHex32(afterBoot); printf((char*)"\n");
        }
        TlsRequire(cpu.live && cpu.systemCalls == 0 && cpu.statistics.yields == 0
            && data[0] == 0x11223344U && data[3] != beforeCpu && cpu.statistics.runTicks > beforeCpuRunTicks,
            "CPU-bound no-syscall peer is genuinely timer-preempted");
        TlsRequire(peer.live && peer.statistics.yields > beforeYields && afterRing0 != beforeRing0
            && afterBoot > beforeBoot && (uint32_t)(afterTicks - beforeTicks) >= 8,
            "TLS peer, real PIT, ring0 and boot survive each victim stop/reap");
        NativeTlsProbeRecord currentPeer = peerRecord;
        TlsInitialized(runtime, tlsPeerId, currentPeer, nativeTlsInputs[7]);
    }
}
asm(".section .text.native_tls_peer,\"ax\"\n.balign 16\n"
    ".global native_user_start,native_user_end\n.type native_user_start,@function\nnative_user_start:\n"
    "incl 0x4000200c\njmp native_user_start\nnative_user_end:\n.size native_user_start,native_user_end-native_user_start\n.text\n");
extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"NATIVE THREAD ID SMOKE BOOT\n");
    GlobalDescriptorTable gdt; TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks); SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    TlsRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "initialize actual physical frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end, (uint32_t)&kernel_readonly_start,
        (uint32_t)&kernel_readonly_end, (const MultibootInfo*)multiboot, 0, 0};
    TlsRequire(paging.prepareIdentity(frames, config), "actual frozen identity template");
    NativeRuntime runtime;
    TlsRequire(runtime.PrepareStacks(paging, frames) && paging.enable() && paging.sealForSharedProcessors()
        && runtime.Activate(tasks, gdt, paging, frames), "real private-PAS CPL3 runtime active");
    kernelDirectory = paging.getStatistics().directoryAddress;
    const uint32_t initial = frames.getStatistics().freeFrames;
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    TlsRequire((boot->flags & 8) && boot->moduleCount == 8, "exact seven genuine victim modules plus mode3peer copy");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    Task ring0(&gdt, Ring0Task); TlsRequire(tasks.AddTask(&ring0), "ring0 progress peer");
    const uint32_t cpuPeer = Create(runtime, 0x11223344, 0);
    uint32_t peerStatic = 0, peerDynamic = 0;
    tlsPeerId = TlsCreate(runtime, modules[7], nativeTlsInputs[7], peerStatic, peerDynamic);
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)0x9B));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)0x2E));
    interrupts.Activate();
    NativeTlsProbeRecord peerRecord = TlsStage(runtime, tasks, tlsPeerId, 1);
    TlsFirstTry(runtime, frames, tlsPeerId, peerRecord, nativeTlsInputs[7], initial - 7, peerStatic, peerDynamic);
    peerRecord = TlsStage(runtime, tasks, tlsPeerId, 2); TlsInitialized(runtime, tlsPeerId, peerRecord, nativeTlsInputs[7]);
    survivorFrames = frames.getStatistics().freeFrames;
    TlsRequire(initial - survivorFrames == 7 + peerStatic + peerDynamic, "actual CPU peer geometry7 plus ELF and heap frames");
    NativeStatus peerStatus = {};
    TlsRequire(runtime.Status(tlsPeerId, peerStatus) && peerStatus.observedCs == 0x23
        && peerStatus.directory == peerStatus.observedCr3 && peerStatus.directory != kernelDirectory,
        "actual long-lived peer has private user CR3");
    uint32_t previousHandle = peerRecord.heap_handle;
    for (uint32_t caseIndex = 0; caseIndex < 8; ++caseIndex) {
        const uint32_t moduleIndex = caseIndex == 7 ? 0 : caseIndex;
        const TlsExpectedInput& expected = nativeTlsInputs[moduleIndex];
        uint32_t data[4]; NativeStatus beforePeer = {}, beforeCpuStatus = {};
        TlsRequire(runtime.ReadMemory(cpuPeer, NativeRuntime::DataAddress, data, sizeof(data))
            && runtime.Status(tlsPeerId, beforePeer) && runtime.Status(cpuPeer, beforeCpuStatus), "independent survivor snapshots");
        const uint32_t beforeCpu = data[3], beforeRing0 = ring0Progress, beforeBoot = tasks.BootTicks(), beforeTicks = tasks.Ticks();
        uint32_t staticCost = 0, dynamicCost = 0;
        const uint32_t id = TlsCreate(runtime, modules[moduleIndex], expected, staticCost, dynamicCost);
        NativeTlsProbeRecord record = TlsStage(runtime, tasks, id, 1);
        TlsFirstTry(runtime, frames, id, record, expected, survivorFrames, staticCost, dynamicCost);
        TlsRequire(record.heap_handle > previousHandle && record.heap_handle > peerRecord.heap_handle,
            "reloaded process obtains fresh nonreused region handle"); previousHandle = record.heap_handle;
        record = TlsStage(runtime, tasks, id, 2); TlsInitialized(runtime, id, record, expected);
        for (uint32_t role = 0; role < 4; ++role)
            TlsRequire(record.objects[role].address == peerRecord.objects[role].address,
                "same TLS VAs with full owner-specific bytes prove private address space isolation");
        if (expected.mode == 3) TlsRequire(runtime.RequestExit(id, 73), "external RequestExit after retained TLS initialization");
        NativeStatus status = WaitStopped(runtime, tasks, id); record = TlsRecord(runtime, id); TlsRecordLog(record, 3);
        TlsRequire(status.observedCs == 0x23 && status.directory == status.observedCr3
            && status.directory != kernelDirectory && status.directory != peerStatus.directory && status.systemCalls,
            "actual stopped victim CS23 and private CR3 distinct from both peer and kernel");
        TlsIdentity(record, expected);
        TlsRequire(record.stage == (expected.mode == 3 ? 2U : 3U),
            "stopped cancellation remains healthy stage2; other terminal paths report stage3");
        const uint32_t retained = survivorFrames - frames.getStatistics().freeFrames;
        TlsRequire(retained == staticCost + (expected.mode == 0 ? 0 : dynamicCost), "independent preReap frame cost matches actual lifecycle");
        TlsControls(runtime, id, record, expected, 3);
        if (expected.mode == 0)
            TlsRequire(status.exitCode == 0 && !status.faultVector && record.stage == 3
                && record.finalize_called && record.finalize_completed && record.heap_empty
                && record.cache_reset_count == 4 && record.initialized_count == 0 && record.final_heap_handle == 0,
                "normal storage finalization releases last-free heap, no destructor claim");
        else if (expected.mode == 1)
            TlsRequire(status.exitCode == GTOS_TLS_PROBE_DIRECT_EXIT && !status.faultVector
                && !record.finalize_called && !record.finalize_completed, "directExit retained TLS without user finalizer");
        else if (expected.mode == 2)
            TlsRequire(status.faultVector == 14 && status.faultAddress == GTOS_TLS_PROBE_GUARD_VA
                && status.faultError == 6 && status.exitCode == 0x8000000EU && !record.finalize_called,
                "exact real PF6 guard and no finalizer");
        else if (expected.mode == 3)
            TlsRequire(status.exitCode == 73 && !status.faultVector && !record.finalize_called, "external cancellation retains TLS for kernelReap");
        else
            TlsRequire(!status.faultVector && status.exitCode == (GTOS_EMUTLS_EXIT_BASE
                    | (expected.mode == 4 ? GTOS_EMUTLS_EXIT_OOM : GTOS_EMUTLS_EXIT_METADATA))
                && !record.finalize_called && record.unpublished_address == 0xA5A5A5A5U
                && record.last_request_control == expected.controls[4].control,
                "honest OOM/metadata fatal with no returned target address and prior full TLS values retained");
        {
            InterruptGuard guard;
            printf((char*)"TLS CASE ordinal="); printfHex32(caseIndex); printf((char*)" mode="); printfHex32(expected.mode);
            printf((char*)" nonce="); printfHex32(expected.nonce); printf((char*)" stage="); printfHex32(record.stage); printf((char*)" cs="); printfHex32(status.observedCs);
            printf((char*)" cr3="); printfHex32(status.observedCr3); printf((char*)" cr2="); printfHex32(status.faultAddress);
            printf((char*)" pf="); printfHex32(status.faultError); printf((char*)" exit="); printfHex32(status.exitCode);
            printf((char*)" static="); printfHex32(staticCost); printf((char*)" dynamic="); printfHex32(dynamicCost);
            printf((char*)" retained="); printfHex32(retained); printf((char*)" yields="); printfHex32(status.statistics.yields);
            printf((char*)" run_ticks="); printfHex32(status.statistics.runTicks); printf((char*)" calls="); printfHex32(status.systemCalls); printf((char*)"\n");
        }
        TlsRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == survivorFrames, "exact deferred victimReap baseline");
        {
            InterruptGuard guard;
            printf((char*)"TLS REAP free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(survivorFrames); printf((char*)"\n");
        }
        TlsSurvivors(runtime, tasks, cpuPeer, peerRecord, beforeCpu, beforeCpuStatus.statistics.runTicks, beforePeer.statistics.yields,
            beforePeer.statistics.runTicks, beforeRing0, beforeBoot, beforeTicks);
    }
    TlsRequire(runtime.RequestExit(tlsPeerId, 74) && runtime.RequestExit(cpuPeer, 0) && runtime.Reap() == 2
        && frames.getStatistics().freeFrames == initial, "final two-peer original frame baseline");
    const uint32_t beforeTicks = tasks.Ticks(), beforeBoot = tasks.BootTicks(), beforeRing0 = ring0Progress;
    WaitTicks(tasks, 8);
    TlsRequire((uint32_t)(tasks.Ticks() - beforeTicks) >= 8 && tasks.BootTicks() > beforeBoot
        && ring0Progress != beforeRing0, "actual PIT/ring0/boot continue after finalReap");
    InterruptGuard guard;
    printf((char*)"TLS FINAL free="); printfHex32(frames.getStatistics().freeFrames); printf((char*)" expected="); printfHex32(initial);
    printf((char*)" tls_admissions=00000009 process_reaps=0000000A victim_cases=00000008 baselines=00000009\n");
    TlsFinish(true);
}
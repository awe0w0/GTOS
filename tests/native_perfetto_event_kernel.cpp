#define NativeProcessSmoke PerfettoEventUnusedBaseline
#include "native_process_smoke.cpp"
#undef NativeProcessSmoke
#include "../apps/native_content_build/gtos-platform/probe/event_record.h"
#include <process/clock_abi.h>
#include <process/native_events.h>

namespace {
    uint8_t eventElf[65536];
    void EventFinish(bool pass) __attribute__((noreturn));
    void EventFinish(bool pass) {
        printf(pass ? (char*)"PERFETTO EVENT GUEST PASS\n" : (char*)"PERFETTO EVENT GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass ? 0x10U : 0x20U), "Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void EventRequire(bool value, const char* reason) {
        if (!value) { printf((char*)"PEVT FAILED "); printf((char*)reason); printf((char*)"\n"); EventFinish(false); }
    }
    void EventCapacity(NativeEvents& events, uint32_t objects, uint32_t handles) {
        const uint32_t owner = 101, peer = 102;
        const int32_t first = events.CreateForOwner(owner, GTOS_EVENT_MANUAL_RESET);
        EventRequire(first > 0, "first owned event");
        for (uint32_t i = 1; i < objects; ++i)
            EventRequire(events.CreateForOwner(owner, 0) > 0, "bounded event capacity");
        EventRequire(events.CreateForOwner(peer, 0) == GTOS_EVENT_ERR_LIMIT,
            "object limit");
        const int32_t alias = events.DuplicateForOwner(owner, first, peer, GTOS_EVENT_RIGHT_WAIT);
        EventRequire(alias > 0 && events.Set(peer, alias) == GTOS_EVENT_ERR_PERMISSION,
            "peer alias signal restriction");
        for (uint32_t i = objects + 1; i < handles; ++i)
            EventRequire(events.DuplicateForOwner(owner, first, owner, 0) > 0,
                "bounded handle capacity");
        EventRequire(events.DuplicateForOwner(owner, first, owner, 0) == GTOS_EVENT_ERR_LIMIT,
            "handle limit");
        const auto full = events.Statistics();
        EventRequire(full.objects == objects && full.handles == handles &&
            full.peakObjects == objects && full.peakHandles == handles, "bounded event statistics");
        EventRequire(events.Set(owner, first) == 0, "owner can signal the shared event");
        events.ReclaimOwner(owner);
        const auto survivor = events.Statistics();
        uint32_t peerHandle = static_cast<uint32_t>(alias);
        EventRequire(survivor.handles == 1 && survivor.objects == 1 &&
            survivor.released == handles - 1 && events.Select(peer, &peerHandle, 1) == 0,
            "reclaim preserves active peer alias");
        EventRequire(events.Close(peer, alias) == 0 && events.CreateForOwner(peer, 0) > alias,
            "capacity reused without recycling the handle identity");
        events.ReclaimOwner(peer);
        const auto empty = events.Statistics();
        EventRequire(!empty.handles && !empty.objects && !events.EnableContentProfile(),
            "event storage restored; profile frozen after use");
    }
}

extern "C" void NativeProcessSmoke(void* multiboot, uint32_t magic) {
    printf((char*)"PERFETTO EVENT GUEST BOOT\n");
    GlobalDescriptorTable gdt;
    TaskManager tasks;
    InterruptsManager interrupts(0x20, &gdt, &tasks);
    SyscallHandler syscalls(&interrupts, 0x80);
    PhysicalMemoryManager frames;
    EventRequire(frames.initialize(multiboot, magic, (uint32_t)&kernel_start, (uint32_t)&kernel_end), "frames");
    KernelPaging paging;
    PagingConfig config = {(uint32_t)&kernel_start, (uint32_t)&kernel_end,
        (uint32_t)&kernel_readonly_start, (uint32_t)&kernel_readonly_end,
        (const MultibootInfo*)multiboot, 0, 0};
    EventRequire(paging.prepareIdentity(frames, config), "identity");
    NativeEvents ordinaryEvents, contentEvents;
    EventCapacity(ordinaryEvents, NativeEvents::MaximumObjects, NativeEvents::MaximumHandles);
    EventRequire(contentEvents.EnableContentProfile() && !contentEvents.EnableContentProfile(),
        "one Content profile selection");
    EventCapacity(contentEvents, NativeEvents::ContentMaximumObjects, NativeEvents::ContentMaximumHandles);
    printf((char*)"NATIVE EVENT CAPACITY PASS\n");
    NativeEvents events;
    NativeRuntime runtime;
    EventRequire(runtime.AttachEvents(events), "explicit native event endpoint");
    EventRequire(runtime.PrepareStacks(paging, frames), "guarded kernel stacks");
    EventRequire(paging.enable() && paging.sealForSharedProcessors(), "frozen paging template");
    EventRequire(runtime.Activate(tasks, gdt, paging, frames, NativeFpSse2), "native SSE2 activation");
    const uint32_t baseline = frames.getStatistics().freeFrames;
    Task ring0(&gdt, Ring0Task);
    EventRequire(tasks.AddTask(&ring0), "independent kernel task");
    const MultibootInfo* boot = (const MultibootInfo*)multiboot;
    EventRequire((boot->flags & 8) && boot->moduleCount == 3, "three current-source modules");
    const MultibootModule* modules = (const MultibootModule*)boot->modules;
    asm volatile("outb %0,$0x43" : : "a"((uint8_t)0x36));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR & 255)));
    asm volatile("outb %0,$0x40" : : "a"((uint8_t)(GTOS_CLOCK_PIT_DIVISOR >> 8)));
    interrupts.Activate();
    for (uint32_t mode = 0; mode < 3; ++mode) {
        const auto before = events.Statistics();
        const MultibootModule& module = modules[mode];
        EventRequire(module.end > module.start && module.end - module.start <= sizeof(eventElf), "bounded ELF");
        const uint32_t bytes = module.end - module.start;
        for (uint32_t index = 0; index < bytes; ++index) eventElf[index] = ((const uint8_t*)module.start)[index];
        Elf32LoadPlan plan;
        EventRequire(ValidateElf32(eventElf, bytes, plan), "native ELF plan");
        uint32_t id;
        EventRequire(runtime.CreateElf(eventElf, bytes, id), "actual ELF admission");
        const NativeStatus status = WaitStopped(runtime, tasks, id);
        gtos_perfetto_event_probe::Record record = {};
        EventRequire(runtime.ReadMemory(id, gtos_perfetto_event_probe::RecordAddress, &record, sizeof(record)), "consumer record");
        const auto current = events.Statistics();
        EventRequire(record.version == 1 && record.mode == mode && !record.failures &&
            record.checks == (mode ? 1U : 72U) && record.waits == (mode ? 0U : 56U) &&
            record.stage == (mode ? 1U : 2U), "consumer result");
        EventRequire(status.observedCs == 0x23 && status.observedCr3 == status.directory &&
            status.directory != paging.getStatistics().directoryAddress &&
            status.systemCalls == (mode == 0 ? 131U : mode == 1 ? 2U : 3U), "CPL3 exact native calls");
        EventRequire(status.exitCode == (mode ? 0x80000006U : 0U) && status.faultVector == (mode ? 6U : 0U), "exact exit/fault policy");
        EventRequire(current.handles == (mode ? 1U : 0U) && current.objects == current.handles &&
            current.created - before.created == (mode ? 1U : 16U) &&
            current.closed - before.closed == (mode ? 0U : 16U) &&
            current.signals - before.signals == (mode ? 0U : 24U), "native service ownership/control");
        printf((char*)"PEVT CASE mode="); printfHex32(mode);
        printf((char*)" checks="); printfHex32(record.checks);
        printf((char*)" cs="); printfHex32(status.observedCs);
        printf((char*)" calls="); printfHex32(status.systemCalls);
        printf((char*)" waits="); printfHex32(record.waits);
        printf((char*)" exit="); printfHex32(status.exitCode);
        printf((char*)" vector="); printfHex32(status.faultVector);
        printf((char*)" handles="); printfHex32(current.handles);
        printf((char*)" created="); printfHex32(current.created - before.created);
        printf((char*)" closed="); printfHex32(current.closed - before.closed);
        printf((char*)" signals="); printfHex32(current.signals - before.signals); printf((char*)"\n");
        EventRequire(runtime.Reap() == 1 && frames.getStatistics().freeFrames == baseline, "exact Reap frame restoration");
        const auto reaped = events.Statistics();
        EventRequire(!reaped.handles && !reaped.objects && reaped.reclaims - before.reclaims == 1 &&
            reaped.released - before.released == (mode ? 1U : 0U), "exact Reap event restoration");
        printf((char*)"PEVT REAP mode="); printfHex32(mode);
        printf((char*)" free="); printfHex32(frames.getStatistics().freeFrames);
        printf((char*)" expected="); printfHex32(baseline);
        printf((char*)" handles="); printfHex32(reaped.handles);
        printf((char*)" released="); printfHex32(reaped.released - before.released); printf((char*)"\n");
    }
    EventRequire(ring0Progress && tasks.BootTicks(), "surviving kernel scheduling");
    EventFinish(true);
}

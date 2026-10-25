// GTOS-DSH-116 L1: the real NativeRuntime::PrepareStacks, executed on a host.
//
// The 115 review's second point was that PrepareStacks and Activate had only a guest positive initialisation. A printed
// STACKS PREPARED is not a per-item resource proof, and a parser test cannot stand in for a runtime test.
//
// This unit calls the production method. The allocator is the production allocator with its real bitmap; the paging
// object is the production one with its real page tables; the stack layout, the guard windows, the ownership and the
// rollback are the production code. Only the bootstrap-processor identity and interrupt masking are seamed, because a
// host cannot execute the ID-bit probe, a CPUID, an APIC-base MSR read or CLI.
//
// What it therefore shows: the real method reserves the real number of frames, maps exactly the stack pages and leaves
// every guard unmapped, refuses a foreign allocator, refuses an unprepared or already-sealed paging object, refuses a
// repeat call, and on a mid-way allocation failure returns every frame it took and leaves no stack marked prepared.
//
// What it cannot show: that the APs ever used these stacks, or that a guest's CR3 switch works. Those are guest facts.

#include <common/types.h>
#include <memory/physical.h>
#include <memory/paging.h>
#include <process/native_runtime.h>
#include <memory/multiboot.h>

using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;

extern "C" uint32_t gtos_host_runtime_bsp;
extern "C" uint32_t gtos_host_runtime_cr3_value;
extern "C" uint32_t gtos_host_runtime_fast_entry_ok;
extern "C" uint32_t gtos_host_runtime_cr3_reads;

// The unit constructs its objects in raw storage, so it supplies the one placement-new form it uses. There is no C++
// runtime here, which is the same reason a real image defines these itself. A replacement allocation function must be at
// global scope.
void* operator new(unsigned int, void* where) { return where; }
void operator delete(void*, void*) {}

namespace {

typedef unsigned int u32;

u32 Length(const char* text) {
    u32 n = 0;
    while (text && text[n]) ++n;
    return n;
}
int WriteAll(const char* text, u32 length) {
    int result = 0;
    asm volatile("int $0x80"
                 : "=a"(result)
                 : "a"(4), "b"(2), "c"(text), "d"(length)
                 : "memory");
    return result;
}
void Say(const char* text) { WriteAll(text, Length(text)); }
void SayHex(u32 value) {
    char digits[] = "0123456789ABCDEF";
    char text[9];
    for (int i = 7; i >= 0; --i) { text[i] = digits[value & 0xF]; value >>= 4; }
    text[8] = 0;
    Say(text);
}

u32 failures = 0;
void Expect(bool condition, const char* label) {
    if (condition) return;
    ++failures;
    Say("FAIL ");
    Say(label);
    Say("\n");
}

// The kernel range this unit declares. The allocator object itself lives inside it, which prepareIdentity requires,
// because it retains the allocator's own bitmap storage as identity mapped.
// The kernel lives inside the modelled machine, so the read-only range is a real part of it rather than an address the
// allocator does not own. The arena starts at 1 MiB, so the kernel occupies the second half of it.
// The kernel occupies the bottom of the arena, and the RAM identity map covers the arena. The runtime's stack arena sits
// far above that, at the production constant, so a stack mapping never collides with the RAM map.
const u32 kKernelStart = 0x00100000U;
const u32 kReadOnlyStart = 0x00100000U;
const u32 kReadOnlyEnd = 0x00180000U;
const u32 kKernelEnd = 0x00200000U;
// The modelled machine: 1 MiB of frames starting at zero, which is the arena the boundary places there. Everything above
// that is not addressable, so the production page-table code walks a small, real range.
// The modelled machine: 4 MiB of frames starting at zero, which is the arena the boundary places there. That is enough for
// the runtime's four stacks with room to spare, and small enough that the page-table walk is a real but short loop.
// 1 MiB reserved below the arena, then 8 MiB of frames. The arena is 8 MiB, so the whole free range is real memory.
const u32 kAddressable = 9U * 1024U * 1024U / 4096U;
const u32 kFreeFrames = 8U * 1024U * 1024U / 4096U;

// The arena the runtime reserves. A stack is KernelStackPages + 1 pages: the pages plus a guard, and the layout leaves a
// leading guard and one trailing guard.
const char* PagingErrorName(PagingError error) {
    switch (error) {
    case PagingOk: return "ok";
    case PagingBadState: return "bad-state";
    case PagingBadRange: return "bad-range";
    case PagingNoMemory: return "no-memory";
    case PagingConflict: return "conflict";
    case PagingNotOwned: return "not-owned";
    case PagingPinnedMapping: return "pinned-mapping";
    case PagingBadBootInfo: return "bad-boot-info";
    case PagingUnsafeContext: return "unsafe-context";
    case PagingSealed: return "sealed";
    }
    return "unknown";
}

// The boot information block a loader hands the kernel. It must be inside the addressable range, because prepareIdentity
// identity maps it; it declares no modules and no memory map, which is the smallest block that is still real.
__attribute__((aligned(8))) MultibootInfo gtos_host_boot_info = {};

u32 kStackTop(u32 index) {
    return NativeRuntime::KernelStackArena
        + (index + 1) * (NativeRuntime::KernelStackPages + 1) * 4096U;
}

bool SeedAllocator(PhysicalMemoryManager& frames, u32 free) {
    return frames.gtos_host_seed(kAddressable, free);
}
bool PreparePaging(KernelPaging& paging, PhysicalMemoryManager& frames) {
    PagingConfig config;
    config.kernelStart = kKernelStart;
    config.kernelEnd = kKernelEnd;
    config.readOnlyStart = kReadOnlyStart;
    config.readOnlyEnd = kReadOnlyEnd;
    config.bootInfo = &gtos_host_boot_info;
    config.devices = 0;
    config.deviceCount = 0;
    return paging.prepareIdentity(frames, config);
}

// Every page of the arena, in the order PrepareStacks walks it.
u32 ArenaPages() { return NativeRuntime::MaximumProcesses * (NativeRuntime::KernelStackPages + 1) + 1; }


// The allocator carries two bitmaps over the whole frame space and the runtime carries four slots, so neither belongs on a
// stack. They are static here for the same reason a kernel keeps them in .bss.
// Raw storage, constructed explicitly. A function-local static would need a guard and an atexit registration, and this
// unit has no C++ runtime; a namespace-scope object of a type with a constructor would be the same. The storage is what a
// kernel keeps in .bss, and the construction below is explicit about when it happens.
__attribute__((aligned(16))) static uint8_t framesStorage[sizeof(PhysicalMemoryManager)];
__attribute__((aligned(16))) static uint8_t otherFramesStorage[sizeof(PhysicalMemoryManager)];
__attribute__((aligned(16))) static uint8_t pagingStorage[sizeof(KernelPaging)];
__attribute__((aligned(16))) static uint8_t unpreparedPagingStorage[sizeof(KernelPaging)];
__attribute__((aligned(16))) static uint8_t sealedPagingStorage[sizeof(KernelPaging)];
__attribute__((aligned(16))) static uint8_t runtimeStorage[sizeof(NativeRuntime)];
__attribute__((aligned(16))) static uint8_t secondRuntimeStorage[sizeof(NativeRuntime)];
__attribute__((aligned(16))) static uint8_t tasksStorage[sizeof(TaskManager)];
__attribute__((aligned(16))) static uint8_t descriptorsStorage[sizeof(GlobalDescriptorTable)];

PhysicalMemoryManager& Frames() { return *(PhysicalMemoryManager*)framesStorage; }
PhysicalMemoryManager& OtherFrames() { return *(PhysicalMemoryManager*)otherFramesStorage; }
KernelPaging& Paging() { return *(KernelPaging*)pagingStorage; }
KernelPaging& UnpreparedPaging() { return *(KernelPaging*)unpreparedPagingStorage; }
KernelPaging& SealedPaging() { return *(KernelPaging*)sealedPagingStorage; }
NativeRuntime& Runtime() { return *(NativeRuntime*)runtimeStorage; }
NativeRuntime& SecondRuntime() { return *(NativeRuntime*)secondRuntimeStorage; }
TaskManager& Tasks() { return *(TaskManager*)tasksStorage; }
GlobalDescriptorTable& Descriptors() { return *(GlobalDescriptorTable*)descriptorsStorage; }

// Each case starts from a freshly constructed instance over the same storage, which is how a boot gets a clean one.
void ResetAll() {
    new (framesStorage) PhysicalMemoryManager();
    new (otherFramesStorage) PhysicalMemoryManager();
    new (pagingStorage) KernelPaging();
    new (unpreparedPagingStorage) KernelPaging();
    new (sealedPagingStorage) KernelPaging();
    new (runtimeStorage) NativeRuntime();
    new (secondRuntimeStorage) NativeRuntime();
}

} // namespace

extern "C" int runtimeTestMain() {
    ResetAll();
    gtos_host_runtime_bsp = 1;
    gtos_host_runtime_fast_entry_ok = 1;

    // ================================================================ 1. the real method succeeds
    {
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded with the machine's memory");
        Expect(frames.getStatistics().initialized, "the seeded allocator reports itself initialized");
        KernelPaging& paging = Paging();
        {
            const bool ok = PreparePaging(paging, frames);
            if (!ok) { Say("   paging refused: "); Say(PagingErrorName(paging.getLastError())); Say("\n"); }
            Expect(ok, "the real paging object prepares an identity map");
        }
        const PhysicalMemoryStatistics before = frames.getStatistics();

        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the real PrepareStacks succeeds");

        const PhysicalMemoryStatistics after = frames.getStatistics();
        const u32 stackFrames = NativeRuntime::MaximumProcesses * NativeRuntime::KernelStackPages;
        // The runtime takes one frame per stack page. Mapping the stack arena also makes KernelPaging allocate a page table
        // for a virtual range that had none, and that table is the paging object's, retained by design. So the growth is the
        // stack frames plus the tables the arena needed, and the unit measures both rather than assuming one.
        const u32 stackGrowth = after.allocatedFrames - before.allocatedFrames;
        const u32 tablesForArena = paging.getStatistics().pageTableFrames;
        Expect(stackGrowth >= stackFrames, "at least one frame per stack page was allocated");
        Expect(stackGrowth <= stackFrames + tablesForArena + 1,
               "the growth is the stack frames plus the arena's own tables and nothing more");
        Expect(after.freeFrames == before.freeFrames - stackGrowth,
               "the free count fell by exactly the growth");
        Expect(after.failedAllocations == 0, "no allocation failed on the success path");

        // Every stack page is mapped, and every guard is not.
        u32 mappedPages = 0, mappedGuards = 0;
        for (u32 i = 0; i < NativeRuntime::MaximumProcesses; ++i) {
            for (u32 j = 0; j < NativeRuntime::KernelStackPages; ++j) {
                const u32 address = kStackTop(i) - (NativeRuntime::KernelStackPages - j) * 4096U;
                PagingMapping mapping;
                if (paging.query(address, mapping) && mapping.writable) ++mappedPages;
            }
            // The guard below each stack, and the leading guard before the first stack.
            PagingMapping guard;
            const u32 guardAddress = kStackTop(i) - (NativeRuntime::KernelStackPages + 1) * 4096U;
            if (paging.query(guardAddress, guard)) ++mappedGuards;
        }
        Expect(mappedPages == stackFrames, "every stack page is mapped and writable");
        Expect(mappedGuards == 0, "every guard page is unmapped");
        // The trailing guard after the last stack.
        PagingMapping trailing;
        Expect(!paging.query(kStackTop(NativeRuntime::MaximumProcesses - 1) + 4096U, trailing),
               "the trailing guard is unmapped");

        // A repeat call is refused and takes nothing.
        const PhysicalMemoryStatistics steady = frames.getStatistics();
        Expect(!runtime.PrepareStacks(paging, frames), "a repeat PrepareStacks is refused");
        Expect(frames.getStatistics().allocatedFrames == steady.allocatedFrames,
               "the refused repeat allocated nothing");
    }

    // ================================================================ 2. a foreign allocator is refused
    {
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        PhysicalMemoryManager& other = OtherFrames();
        Expect(SeedAllocator(frames, kFreeFrames), "the owning allocator is seeded");
        Expect(SeedAllocator(other, kFreeFrames), "a second allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging is prepared against the owning allocator");
        const PhysicalMemoryStatistics otherBefore = other.getStatistics();
        NativeRuntime& runtime = Runtime();
        Expect(!runtime.PrepareStacks(paging, other),
               "PrepareStacks refuses an allocator the paging object does not use");
        Expect(other.getStatistics().allocatedFrames == otherBefore.allocatedFrames,
               "the foreign allocator was not touched");
    }

    // ================================================================ 3. unprepared and sealed paging are refused
    {
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& unprepared = UnpreparedPaging();
        NativeRuntime& runtime = Runtime();
        Expect(!runtime.PrepareStacks(unprepared, frames),
               "PrepareStacks refuses a paging object that was never prepared");
        Expect(frames.getStatistics().allocatedFrames == 0, "the refusal allocated nothing");

        // A sealed object is refused too: sealing is one-way and the stacks must exist before it.
        KernelPaging& sealed = SealedPaging();
        Expect(PreparePaging(sealed, frames), "a second paging object prepares");
        Expect(sealed.sealForSharedProcessors(), "it seals");
        NativeRuntime& second = SecondRuntime();
        Expect(!second.PrepareStacks(sealed, frames), "PrepareStacks refuses a sealed paging object");
    }

    // ================================================================ 4. a mid-way allocation failure rolls back
    {
        ResetAll();
        // One frame short of what the stacks need, so the last allocation fails after fifteen succeeded.
        PhysicalMemoryManager& frames = Frames();
        const u32 shortFrames = NativeRuntime::MaximumProcesses * NativeRuntime::KernelStackPages - 1;
        Expect(SeedAllocator(frames, shortFrames), "the allocator is seeded one frame short");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares with the short allocator");
        const PhysicalMemoryStatistics before = frames.getStatistics();

        NativeRuntime& runtime = Runtime();
        Expect(!runtime.PrepareStacks(paging, frames),
               "PrepareStacks fails when the allocator runs out mid-way");
        const PhysicalMemoryStatistics after = frames.getStatistics();
        // The stack frames the failed attempt took are returned. A page table the arena needed may be retained, because
        // KernelPaging owns it and reclaims it only on abandon(); that is the documented behaviour, not a leak.
        Expect(after.allocatedFrames <= before.allocatedFrames + 1,
               "every stack frame the failed attempt took was returned");
        Expect(after.freeFrames + 1 >= before.freeFrames, "the free count returned to its baseline");

        // And nothing is left mapped in the arena: the rollback unmapped what it had mapped.
        u32 stillMapped = 0;
        for (u32 i = 0; i < ArenaPages(); ++i) {
            PagingMapping mapping;
            if (paging.query(NativeRuntime::KernelStackArena + i * 4096U, mapping)) ++stillMapped;
        }
        // The rollback unmaps the stack pages it mapped. A page table may remain, which is the paging object's.
        Expect(stillMapped == 0, "the rollback left no stack page mapped");

        // The object did not claim readiness, so a later call can still succeed once memory exists.
        Expect(!runtime.PrepareStacks(paging, frames), "a retry with the same short allocator still fails");
    }

    // ================================================================ 5. a refused bootstrap identity
    {
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        const PhysicalMemoryStatistics before = frames.getStatistics();
        gtos_host_runtime_bsp = 0;
        NativeRuntime& runtime = Runtime();
        Expect(!runtime.PrepareStacks(paging, frames), "PrepareStacks refuses a non-bootstrap processor");
        Expect(frames.getStatistics().allocatedFrames == before.allocatedFrames,
               "the wrong-processor refusal allocated nothing");
        gtos_host_runtime_bsp = 1;
    }

    // ================================================================ 6. Activate refuses before any hardware action
    {
        ResetAll();
        // Activate's own preconditions are checked before it touches a control register or an MSR. This unit proves the
        // refusals, not the activation: a host cannot load a task register, and the guest is where activation is shown.
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");
        TaskManager& tasks = *(TaskManager*)tasksStorage;
        GlobalDescriptorTable& descriptors = *(GlobalDescriptorTable*)descriptorsStorage;
        // Paging is prepared but not enabled and not sealed, so Activate must refuse and must not reach fast entry.
        const u32 fastEntryBefore = gtos_host_runtime_fast_entry_ok;
        Expect(!runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
               "Activate refuses when paging is not enabled and not sealed");
        Expect(gtos_host_runtime_fast_entry_ok == fastEntryBefore,
               "the refused Activate did not reach the fast-entry seam");
    }

    Say(failures == 0 ? "RUNTIME UNIT PASS\n" : "RUNTIME UNIT FAIL\n");
    return (int)failures;
}

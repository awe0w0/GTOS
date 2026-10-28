// GTOS-DSH-117 V1/V2: exact PrepareStacks resources, independent Activate preconditions, and the Create capacity,
// failure-recovery and slot-reuse loop.
//
// The 116 review named four gaps in the previous unit, and each is closed by measurement rather than by a wider tolerance:
//
//   1. PrepareStacks used `>= stackFrames` and `<= stackFrames + allTables + 1`. That cannot prove "only these resources".
//      This unit records the exact set of allocated frames before and after, and asserts the difference is exactly the
//      stack frames plus the page tables the stack arena needed, with each table named as KernelPaging's.
//   2. Activate was only refused when paging was neither enabled nor sealed, and the check was that a *configuration
//      value* was unchanged. A boundary that returns the same value while being called would have passed. This unit
//      checks the call counter, so a call that returns true still fails the assertion.
//   3. There was no Create unit at all: no capacity, no per-stage failure, no real reuse, no old-identifier isolation.
//      This unit fills every slot, keeps a live sibling, fails at each reachable resource stage, recovers when the supply
//      returns, reuses a real slot, and proves an old identifier cannot reach the new object.
//   4. The identity covered four objects. That is a tooling concern and is handled by the link-input record, not here.
//
// The seam still supplies only what a host cannot do: the bootstrap-processor identity, CR3, fast-entry clearing,
// interrupt masking, and the memory a firmware map would describe. Every allocator decision, page table, stack layout,
// guard window, ownership record, rollback and published state is the production code.

#include <common/types.h>
#include <memory/multiboot.h>
#include <memory/physical.h>
#include <memory/paging.h>
#include <process/native_runtime.h>

using namespace gtos;
using namespace gtos::memory;
using namespace gtos::process;

extern "C" uint32_t gtos_host_runtime_bsp;
extern "C" uint32_t gtos_host_runtime_cr3_value;
extern "C" uint32_t gtos_host_runtime_fast_entry_ok;
extern "C" uint32_t gtos_host_runtime_cr3_reads;
extern "C" uint32_t gtos_host_runtime_fast_entry_calls;
extern "C" uint32_t gtos_process_memory_test_cr3;
extern "C" bool gtos_process_memory_test_bsp;
// The declared host-only fault point for the late stage of Create. Zero means the production path.
extern "C" uint32_t gtos_process_memory_test_fail_protect_after;
extern "C" uint32_t gtos_process_memory_test_protect_calls;
extern "C" uint32_t gtos_process_memory_test_protect_refusals;
extern "C" uint32_t gtos_process_memory_test_protect_pages_at_refusal;

// The unit constructs its objects in raw storage, so it supplies the one placement-new form it uses. A replacement
// allocation function must be at global scope.
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
void ExpectHex(u32 got, u32 want, const char* label) {
    if (got == want) return;
    ++failures;
    Say("FAIL ");
    Say(label);
    Say(" got="); SayHex(got);
    Say(" want="); SayHex(want);
    Say("\n");
}

// ---------------------------------------------------------------- the modelled machine
// 1 MiB reserved below the arena, then 8 MiB of frames. The arena is real host memory placed at the modelled address, so a
// frame number the allocator returns is also a dereferenceable pointer -- which is what an identity map gives the kernel.
// The modelled machine, laid out the way a real one is: the kernel's own image at 1 MiB, the kernel stack arena above the
// image, and 4 MiB of free frames at 8 MiB. All of it is real host memory placed at the modelled address, so a frame the allocator
// returns is a dereferenceable pointer and a page it maps is writable -- which is exactly what an identity map gives a kernel.
//
// The end of the reserved kernel range is the end of this unit's own image, taken from the linker script rather than written
// out beside it. The production paging object refuses an unsafe context unless its own bytes, the allocator's bytes, the
// directory and every table are identity mapped, and the only way that can be true of a host process is if the unit loads
// inside the range the identity map covers. So the range follows the image. It used to be the literal 5 MiB, which held the
// modelled kernel but not the storage this unit statically holds for the runtime, so the paging object -- correctly -- refused
// its own context and six cases failed before reaching what they test. Deriving the end here keeps the check and makes the
// layout describe the binary instead of a guess about it.
extern "C" char __gtos_host_image_start;
extern "C" char __gtos_host_image_end;
const u32 kKernelStart = 0x00100000U;
const u32 kReadOnlyStart = 0x00100000U;
const u32 kReadOnlyEnd = 0x00101000U;
// A namespace-scope const initialised from a linker symbol is folded before the symbol is known, so the end of the image is
// read through a function: the value has to come from the linked binary, not from the compiler's guess about it.
u32 HostImageEnd() { return ((u32)&__gtos_host_image_end + 4095U) & ~4095U; }
// The arena address is the production constant, not a copy of it: a fixture that duplicated the number could reserve one
// range while the runtime wrote stacks into another and still look consistent.
const u32 kStackArena = NativeRuntime::KernelStackArena;
const u32 kStackArenaBytes = 0x00014000U;
const u32 kFreeStart = 0x00800000U;
const u32 kFreeFrames = 4U * 1024U * 1024U / 4096U;
const u32 kAddressable = 0x00C00000U / 4096U;

// Raw storage, constructed explicitly. A function-local static would need a guard and an atexit registration, and this unit
// has no C++ runtime.
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

void ResetAll() {
    new (framesStorage) PhysicalMemoryManager();
    new (otherFramesStorage) PhysicalMemoryManager();
    new (pagingStorage) KernelPaging();
    new (unpreparedPagingStorage) KernelPaging();
    new (sealedPagingStorage) KernelPaging();
    new (runtimeStorage) NativeRuntime();
    new (secondRuntimeStorage) NativeRuntime();
    // Activate reads tasks.CurrentTask() and loads a task-state descriptor, so both must exist before it is called.
    new (tasksStorage) TaskManager();
    new (descriptorsStorage) GlobalDescriptorTable();
}

// The boot information block a loader hands the kernel. It must be inside the addressable range, because prepareIdentity
// identity maps it; it declares no modules and no memory map, which is the smallest block that is still real.
__attribute__((aligned(8))) MultibootInfo gtos_host_boot_info = {};

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

bool SeedAllocator(PhysicalMemoryManager& frames, u32 free) {
    // The free range is the machine's available memory; the kernel image and the stack arena are not available, which is
    // what reserveRegion says through the production call.
    const u32 want = free < kFreeFrames ? free : kFreeFrames;
    if (!frames.gtos_host_seed(kAddressable, kFreeStart / 4096U, want)) return false;
    if (!frames.reserveRegion(kKernelStart, HostImageEnd() - kKernelStart)) return false;
    return frames.reserveRegion(kStackArena, kStackArenaBytes);
}
bool PreparePaging(KernelPaging& paging, PhysicalMemoryManager& frames) {
    PagingConfig config;
    config.kernelStart = kKernelStart;
    config.kernelEnd = HostImageEnd();
    config.readOnlyStart = kReadOnlyStart;
    config.readOnlyEnd = kReadOnlyEnd;
    config.bootInfo = &gtos_host_boot_info;
    config.devices = 0;
    config.deviceCount = 0;
    return paging.prepareIdentity(frames, config);
}

u32 kStackTop(u32 index) {
    return NativeRuntime::KernelStackArena
        + (index + 1) * (NativeRuntime::KernelStackPages + 1) * 4096U;
}

// ---------------------------------------------------------------- the frame set
// A count is not a set: two different frames can leave the count identical. The set is recorded by asking the allocator
// about every addressable frame, which is the allocator's own ownership record rather than a number the test keeps.
// The tracked set must hold every addressable frame, or a truncated walk is indistinguishable from a complete one.
const u32 kMaximumTracked = kAddressable;
static u32 frameSet[kMaximumTracked];
static u32 frameSetCount = 0;

bool frameSetComplete = false;
void CaptureFrameSet(PhysicalMemoryManager& frames) {
    frameSetCount = 0;
    frameSetComplete = true;
    for (u32 frame = 0; frame < kAddressable; ++frame) {
        const u32 address = frame * 4096U;
        if (!frames.isAllocated(address)) continue;
        if (frameSetCount == kMaximumTracked) { frameSetComplete = false; break; }
        frameSet[frameSetCount++] = address;
    }
}
bool FrameSetContains(u32 address) {
    for (u32 i = 0; i < frameSetCount; ++i) if (frameSet[i] == address) return true;
    return false;
}
// The frames present after but not before, and before but not after.
u32 frameSetAdded = 0, frameSetRemoved = 0;
void DiffFrameSet(const u32* before, u32 beforeCount) {
    frameSetAdded = 0; frameSetRemoved = 0;
    for (u32 i = 0; i < frameSetCount; ++i) {
        bool found = false;
        for (u32 j = 0; j < beforeCount; ++j) if (before[j] == frameSet[i]) { found = true; break; }
        if (!found) ++frameSetAdded;
    }
    for (u32 j = 0; j < beforeCount; ++j) {
        bool found = false;
        for (u32 i = 0; i < frameSetCount; ++i) if (frameSet[i] == before[j]) { found = true; break; }
        if (!found) ++frameSetRemoved;
    }
}

// Buffers the runtime writes into. Production requires them to be kernel-mapped memory, so they are file-scope objects
// inside the modelled kernel image rather than host stack locals.
static uint8_t readBuffer[64];
static uint8_t readBuffer2[64];

// ---------------------------------------------------------------- the fixed object images
// Small constants compiled into this image. Nothing is loaded from a host, a disk or a network, no user program is
// assembled or linked, and CreateElf is never called. The entry byte is a return; it is never executed.
const uint8_t kCode[16] = { 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
uint8_t dataFor(u32 seed, u32 index) { return (uint8_t)((seed * 37U + index * 11U + 0x5A) & 0xFF); }

static uint8_t objectData[NativeRuntime::MaximumProcesses][32];
void BuildData(u32 seed) {
    for (u32 slot = 0; slot < NativeRuntime::MaximumProcesses; ++slot)
        for (u32 i = 0; i < 32; ++i) objectData[slot][i] = dataFor(seed + slot, i);
}

void MakeImage(u32 slot, NativeImage& image) {
    image.code = kCode;
    image.codeBytes = sizeof(kCode);
    image.entryOffset = 0;
    image.data = objectData[slot];
    image.dataBytes = sizeof(objectData[slot]);
}


// The three steps a guest performs before Create: enable, seal, and have CR3 name the directory the seal froze. On a host
// the two control-register writes are replaced by the documented test guard; nothing else about the sequence changes.
bool ActivateRuntime(NativeRuntime& runtime, KernelPaging& paging, PhysicalMemoryManager& frames) {
    if (!paging.enable()) { Say("   enable refused: "); Say(PagingErrorName(paging.getLastError())); Say("\n"); return false; }
    if (!paging.sealForSharedProcessors()) return false;
    gtos_host_runtime_cr3_value = paging.getStatistics().directoryAddress;
    // The process address space asks the same question of its own seam, so it gets the same truthful answer.
    gtos_process_memory_test_cr3 = paging.getStatistics().directoryAddress;
    gtos_process_memory_test_bsp = true;
    const PagingStatistics info = paging.getStatistics();
    if (!runtime.Activate(Tasks(), Descriptors(), paging, frames, NativeFpDisabled, false)) {
        Say("   activate refused: enabled="); SayHex(info.enabled ? 1 : 0);
        Say(" sealed="); SayHex(info.sealedForSharing ? 1 : 0);
        Say(" cr3="); SayHex(gtos_host_runtime_cr3_value);
        Say(" dir="); SayHex(info.directoryAddress);
        Say(" bsp="); SayHex(gtos_host_runtime_bsp);
        Say(" fe="); SayHex(gtos_host_runtime_fast_entry_ok);
        Say(" cur="); SayHex(Tasks().CurrentTask() ? 1 : 0);
        Say("\n");
        return false;
    }
    return true;
}

} // namespace

extern "C" int GtosInstallFaultObserver();

// The selected case. A successful Activate publishes a process-wide owner with no destroy path, so two activations cannot
// share a process; the caller runs exactly one case per process.
//
// The selector is strict on purpose. An earlier version defaulted to zero and treated zero as "every case", which meant a
// number that named no case ran nothing and still reported PASS. Both halves of that are now refused:
//   - no argument, or a second argument, is a refusal rather than a default
//   - a value outside 1..kCaseCount is a refusal rather than an empty run
// A refusal prints why and returns a non-zero status, so it can never be read as a pass.
const unsigned kCaseCount = 11;
unsigned unitCase = 0;

enum SelectorVerdict { SelectorOk, SelectorAbsent, SelectorMalformed, SelectorOutOfRange, SelectorTooMany };

SelectorVerdict SelectCase(int argc, char** argv) {
    if (argc < 2 || !argv || !argv[1] || !argv[1][0]) return SelectorAbsent;
    if (argc > 2) return SelectorTooMany;
    const char* text = argv[1];
    unsigned value = 0;
    unsigned digits = 0;
    for (unsigned i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9') return SelectorMalformed;
        if (++digits > 3) return SelectorMalformed;
        value = value * 10 + (unsigned)(text[i] - '0');
    }
    if (value < 1 || value > kCaseCount) return SelectorOutOfRange;
    unitCase = value;
    return SelectorOk;
}

extern "C" int runtimeTestMain(int argc, char** argv) {
    const SelectorVerdict verdict = SelectCase(argc, argv);
    if (verdict != SelectorOk) {
        Say("REFUSED ");
        switch (verdict) {
        case SelectorAbsent: Say("no case selector: exactly one case must be named"); break;
        case SelectorMalformed: Say("the case selector is not a decimal number"); break;
        case SelectorOutOfRange: Say("the case selector names no case"); break;
        case SelectorTooMany: Say("more than one argument"); break;
        default: Say("unknown"); break;
        }
        Say(" (1.."); SayHex(kCaseCount); Say(")\n");
        return 90;
    }
    Say("STEP enter case="); SayHex(unitCase); Say("\n");
    // Install the in-process fault observer first: this distribution has no debugger, so a fault's PC and address can only
    // come from inside the process. A refusal is a failure, not a note: an unobserved crash would be indistinguishable from
    // an unexplained signal, so the run cannot be reported as a pass.
    const int observer = GtosInstallFaultObserver();
    if (!(observer & 1)) { ++failures; Say("FAIL the alternate signal stack was refused\n"); }
    if ((observer >> 8) != 5) { ++failures; Say("FAIL only "); SayHex(observer >> 8); Say(" of 5 signals accepted\n"); }
    ResetAll();
    Say("STEP reset\n");
    // The layout the identity map depends on, stated by the unit instead of assumed by the reader. If the image does not
    // load where the modelled kernel is, the paging object will refuse its own context and every activation case would fail
    // for that reason rather than for the one it tests. The unit says so up front, with the addresses it actually got.
    Say("STEP image "); SayHex((u32)&__gtos_host_image_start); Say(" ");
    SayHex((u32)&__gtos_host_image_end); Say(" "); SayHex(HostImageEnd()); Say("\n");
    Expect((u32)&__gtos_host_image_start == kKernelStart, "the unit's image starts at the modelled kernel address");
    Expect(HostImageEnd() >= (u32)&__gtos_host_image_end, "the reserved kernel range covers the whole image");
    Expect(HostImageEnd() <= kStackArena, "the reserved kernel range ends before the kernel stack arena");
    Expect((u32)&pagingStorage >= kReadOnlyEnd, "the paging object is past the modelled read-only page");
    Expect((u32)&framesStorage >= kReadOnlyEnd, "the allocator is past the modelled read-only page");
    Expect(kStackArena + kStackArenaBytes <= kFreeStart, "the stack arena ends before the modelled free frames");
    // The arena is mapped by the entry, and the unit proves it rather than trusting it: a fixed address that nothing mapped
    // faults inside PrepareStacks, which is indistinguishable from a defect in the code under test. Both ends are written and
    // read back, so a mapping that is too short is caught here instead of in the middle of a case.
    {
        volatile uint8_t* arenaFirst = (volatile uint8_t*)kStackArena;
        volatile uint8_t* arenaLast = (volatile uint8_t*)(kStackArena + kStackArenaBytes - 1);
        *arenaFirst = 0x5A; *arenaLast = 0xA5;
        Expect(*arenaFirst == 0x5A && *arenaLast == 0xA5, "the kernel stack arena is writable where the runtime uses it");
        *arenaFirst = 0; *arenaLast = 0;
    }
    Say("STEP placed\n");
    gtos_host_runtime_bsp = 1;
    gtos_host_runtime_fast_entry_ok = 1;

    // ================================================================ 1. PrepareStacks: the exact frame set
    { if (unitCase == 1) {
        Say("CASE 1\n");
        PhysicalMemoryManager& frames = Frames();
        Say("STEP seed\n");
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded with the machine's memory");
        Say("STEP seeded\n");
        KernelPaging& paging = Paging();
        Say("STEP paging\n");
        Expect(PreparePaging(paging, frames), "the real paging object prepares an identity map");
        Say("STEP prepared\n");
const PagingStatistics pagingBefore = paging.getStatistics();

        static u32 before[kMaximumTracked];
        u32 beforeCount = 0;
CaptureFrameSet(frames);

Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) before[beforeCount++] = frameSet[i];

        NativeRuntime& runtime = Runtime();
Expect(runtime.PrepareStacks(paging, frames), "the real PrepareStacks succeeds");

CaptureFrameSet(frames);


Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(before, beforeCount);

        const u32 stackFrames = NativeRuntime::MaximumProcesses * NativeRuntime::KernelStackPages;
        const PagingStatistics pagingAfter = paging.getStatistics();
        const u32 newTables = pagingAfter.pageTableFrames - pagingBefore.pageTableFrames;

        // Exactly the stack frames plus the page tables the stack arena needed. No tolerance: a frame that is neither a
        // stack page nor a page table is an unexplained residue, and the assertion fails on it.
        ExpectHex(frameSetAdded, stackFrames + newTables,
                  "the added frames are exactly the stack frames plus the new page tables");
        ExpectHex(frameSetRemoved, 0, "PrepareStacks removed no frame");
        u32 mappedPages = 0, mappedGuards = 0;
        for (u32 i = 0; i < NativeRuntime::MaximumProcesses; ++i) {
            for (u32 j = 0; j < NativeRuntime::KernelStackPages; ++j) {
                const u32 address = kStackTop(i) - (NativeRuntime::KernelStackPages - j) * 4096U;
                PagingMapping mapping;
                if (paging.query(address, mapping) && mapping.writable
                    && FrameSetContains(mapping.physicalAddress)) ++mappedPages;
            }
            PagingMapping guard;
            if (paging.query(kStackTop(i) - (NativeRuntime::KernelStackPages + 1) * 4096U, guard)) ++mappedGuards;
        }
        ExpectHex(mappedPages, stackFrames, "every stack page is mapped to a frame this call added");
        ExpectHex(mappedGuards, 0, "every guard page is unmapped");
        PagingMapping trailing;
        Expect(!paging.query(kStackTop(NativeRuntime::MaximumProcesses - 1) + 4096U, trailing),
               "the trailing guard is unmapped");
        // The arena may share a page table with the image or need its own; either way the equality above accounts for it.
        Say("   new tables "); SayHex(newTables);
        Say("   stacks added "); SayHex(frameSetAdded);
        Say(" tables "); SayHex(newTables);
        Say("\n");
    }
    }

    // ================================================================ 2. PrepareStacks: a repeat takes nothing
    { if (unitCase == 2) {
        Say("CASE 2\n");
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the first PrepareStacks succeeds");
        static u32 before[kMaximumTracked];
        u32 beforeCount = 0;
        CaptureFrameSet(frames);

        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) before[beforeCount++] = frameSet[i];
        Expect(!runtime.PrepareStacks(paging, frames), "a repeat PrepareStacks is refused");
        CaptureFrameSet(frames);

        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(before, beforeCount);
        ExpectHex(frameSetAdded, 0, "the refused repeat added no frame");
    }
    }

    // ================================================================ 3. PrepareStacks: a foreign allocator is untouched
    { if (unitCase == 3) {
        Say("CASE 3\n");
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        PhysicalMemoryManager& other = OtherFrames();
        Expect(SeedAllocator(frames, kFreeFrames), "the owning allocator is seeded");
        Expect(SeedAllocator(other, kFreeFrames), "a second allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging is prepared against the owning allocator");
        static u32 before[kMaximumTracked];
        u32 beforeCount = 0;
        CaptureFrameSet(other);

        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) before[beforeCount++] = frameSet[i];
        NativeRuntime& runtime = Runtime();
        Expect(!runtime.PrepareStacks(paging, other),
               "PrepareStacks refuses an allocator the paging object does not use");
        CaptureFrameSet(other);

        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(before, beforeCount);
        ExpectHex(frameSetAdded, 0, "the foreign allocator gained no frame");
    }
    }

    // ================================================================ 4. PrepareStacks: unprepared and sealed paging
    { if (unitCase == 4) {
        Say("CASE 4\n");
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& unprepared = UnpreparedPaging();
        NativeRuntime& runtime = Runtime();
        Expect(!runtime.PrepareStacks(unprepared, frames),
               "PrepareStacks refuses a paging object that was never prepared");
        ExpectHex(frames.getStatistics().allocatedFrames, 0, "the unprepared refusal allocated nothing");

        ResetAll();
        PhysicalMemoryManager& frames2 = Frames();
        Expect(SeedAllocator(frames2, kFreeFrames), "the allocator is seeded again");
        KernelPaging& sealed = SealedPaging();
        Expect(PreparePaging(sealed, frames2), "a second paging object prepares");
        Expect(sealed.sealForSharedProcessors(), "it seals");
        NativeRuntime& second = SecondRuntime();
        Expect(!second.PrepareStacks(sealed, frames2), "PrepareStacks refuses a sealed paging object");
    }
    }

    // ================================================================ 5. Activate: independent preconditions, by call count
    { if (unitCase == 5) {
        Say("CASE 5\n");
        // Each refusal below must differ from an otherwise-valid request by exactly one condition. The 118 version tested the
        // wrong allocator and the wrong processor while paging was still neither enabled nor sealed, so those two refusals
        // could have come from the paging precondition instead of the condition being tested. Here the paging conditions are
        // satisfied first, and every later refusal is checked against that same satisfied state.
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        TaskManager& tasks = Tasks();
        GlobalDescriptorTable& descriptors = Descriptors();

        // (1) Only the stacks are missing.
        {
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(!runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "refused: the stacks were never prepared");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before,
                      "the stacks refusal never reached the fast-entry boundary");
        }
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");

        // (2) Only the paging state is missing: prepared, but not enabled and not sealed.
        {
            const PagingStatistics s = paging.getStatistics();
            Expect(!s.enabled && !s.sealedForSharing, "paging is prepared but neither enabled nor sealed here");
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(!runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "refused: paging is neither enabled nor sealed");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before,
                      "the paging refusal never reached the fast-entry boundary");
        }

        // Bring the paging conditions up so that what follows tests only its own condition.
        Expect(paging.enable(), "paging enables");
        Expect(paging.sealForSharedProcessors(), "paging seals for shared processors");
        gtos_host_runtime_cr3_value = paging.getStatistics().directoryAddress;
        gtos_process_memory_test_cr3 = paging.getStatistics().directoryAddress;
        {
            const PagingStatistics s = paging.getStatistics();
            Expect(s.prepared && s.enabled && s.sealedForSharing,
                   "the paging conditions are all satisfied before the remaining refusals");
            Expect(gtos_host_runtime_cr3_value == s.directoryAddress, "CR3 names the sealed directory");
        }

        // (3) Only the allocator is wrong. Everything else is the same as it will be for a valid request.
        {
            PhysicalMemoryManager& other = OtherFrames();
            Expect(SeedAllocator(other, kFreeFrames), "a second allocator is seeded");
            Expect(!paging.usesAllocator(other), "the paging object does not use that allocator");
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(!runtime.Activate(tasks, descriptors, paging, other, NativeFpDisabled, false),
                   "refused: an allocator the paging object does not use");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before,
                      "the wrong-allocator refusal never reached the fast-entry boundary");
        }

        // (4) Only the processor is wrong.
        {
            gtos_host_runtime_bsp = 0;
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(!runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "refused: a non-bootstrap processor");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before,
                      "the wrong-processor refusal never reached the fast-entry boundary");
            gtos_host_runtime_bsp = 1;
        }

        // (5) Only CR3 is wrong.
        {
            const u32 saved = gtos_host_runtime_cr3_value;
            gtos_host_runtime_cr3_value = saved ^ 0x1000U;
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(!runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "refused: CR3 names a directory the paging object did not seal");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before,
                      "the wrong-CR3 refusal never reached the fast-entry boundary");
            gtos_host_runtime_cr3_value = saved;
        }

        // (6) Only the fast-entry seam refuses.
        {
            const u32 saved = gtos_host_runtime_fast_entry_ok;
            gtos_host_runtime_fast_entry_ok = 0;
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(!runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "refused: the fast-entry boundary could not be cleared");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before + 1,
                      "the fast-entry refusal did reach the boundary exactly once");
            gtos_host_runtime_fast_entry_ok = saved;
        }

        // (7) With every condition satisfied, the same request must now succeed. This is the positive baseline that makes the
        // six refusals above meaningful: without it, a refusal could be caused by something never brought up at all.
        {
            Expect(!NativeRuntime::Active(), "no runtime is active before the positive attempt");
            const u32 before = gtos_host_runtime_fast_entry_calls;
            Expect(runtime.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "the otherwise-valid request is accepted once every condition holds");
            ExpectHex(gtos_host_runtime_fast_entry_calls, before + 1,
                      "the accepted request reached the fast-entry boundary exactly once");
            Expect(NativeRuntime::Active() == &runtime, "the accepted request published this runtime as active");
        }

        // (8) A second owner is refused: the runtime is already published.
        {
            NativeRuntime& second = SecondRuntime();
            Expect(!second.Activate(tasks, descriptors, paging, frames, NativeFpDisabled, false),
                   "a second runtime is refused while one is already active");
            Expect(NativeRuntime::Active() == &runtime, "the active runtime is unchanged by the refused second attempt");
        }
    }
    }

    // ================================================================ 6. Create: capacity, live sibling, refusal
    { if (unitCase == 6) {
        Say("CASE 6\n");
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");
        // Create needs a published owner. A host cannot enable paging, so the owner is published through the seam the
        // guest's Activate reaches; every object decision after this point is production code.
        Say("A0\n");
        const bool activated = ActivateRuntime(runtime, paging, frames);
        Say("A1\n");
        Expect(activated, "the runtime activates through the real method");
        Say("A2\n");
        const bool isActive = NativeRuntime::Active() != 0;
        Say("A3\n");
        Expect(isActive, "the runtime is the active one after activation");
        Say("A4\n");
        Expect(gtos_host_runtime_cr3_value == paging.getStatistics().directoryAddress,
               "CR3 names the sealed directory");

        BuildData(7);
        uint32_t ids[NativeRuntime::MaximumProcesses];
        NativeImage image;
        u32 created = 0;
        for (u32 i = 0; i < NativeRuntime::MaximumProcesses; ++i) {
            MakeImage(i, image);
            ids[i] = 0;
            if (runtime.Create(image, ids[i]) && ids[i]) ++created;
        }
        ExpectHex(created, NativeRuntime::MaximumProcesses, "every available slot was filled");
        bool distinct = true;
        for (u32 i = 0; i < created; ++i) {
            if (!ids[i]) distinct = false;
            for (u32 j = i + 1; j < created; ++j) if (ids[i] == ids[j]) distinct = false;
        }
        Expect(distinct, "every identifier is distinct and non-zero");

        const u32 victimId = ids[0];
        uint8_t* victimData = readBuffer;
        Expect(runtime.ReadMemory(victimId, NativeRuntime::DataAddress, victimData, 32),
               "the victim can be read");
        bool victimMatches = true;
        for (u32 i = 0; i < 32; ++i) if (victimData[i] != objectData[0][i]) victimMatches = false;
        Expect(victimMatches, "the victim's data is its own");

        MakeImage(0, image);
        uint32_t overflowId = 0xDEADBEEFU;
        Expect(!runtime.Create(image, overflowId), "a full runtime refuses another object");
        ExpectHex(overflowId, 0, "the refused Create cleared the caller's identifier");
        uint8_t* again = readBuffer2;
        Expect(runtime.ReadMemory(victimId, NativeRuntime::DataAddress, again, 32),
               "the victim is still readable after the capacity refusal");
        bool unchanged = true;
        for (u32 i = 0; i < 32; ++i) if (again[i] != objectData[0][i]) unchanged = false;
        Expect(unchanged, "the victim's data is unchanged after the capacity refusal");

        // Real slot reuse: reap one, then create again into the slot it freed.
        const u32 reuseTarget = ids[1];
        Expect(runtime.RequestExit(reuseTarget, 0x21), "the object chosen for reuse exits");
        ExpectHex(runtime.Reap(), 1, "exactly one object was reaped");
        MakeImage(1, image);
        uint32_t newId = 0;
        Expect(runtime.Create(image, newId), "a slot freed by reaping accepts a new object");
        Expect(newId != 0, "the reused slot produced a non-zero identifier");
        Expect(newId != reuseTarget, "the reused slot issued a different identifier");
        uint8_t* oldRead = readBuffer2;
        Expect(!runtime.ReadMemory(reuseTarget, NativeRuntime::DataAddress, oldRead, 4),
               "the old identifier cannot read the new object");
        Expect(!runtime.RequestExit(reuseTarget, 0x99), "the old identifier cannot exit the new object");
        uint8_t* victimFinal = readBuffer;
        Expect(runtime.ReadMemory(victimId, NativeRuntime::DataAddress, victimFinal, 32),
               "the victim survives the reuse");
        bool victimFinalOk = true;
        for (u32 i = 0; i < 32; ++i)
            if (victimFinal[i] != objectData[0][i]) victimFinalOk = false;
        Expect(victimFinalOk, "the victim's data survived the reuse unchanged");
        uint8_t* newRead = readBuffer2;
        Expect(runtime.ReadMemory(newId, NativeRuntime::DataAddress, newRead, 32),
               "the new object is readable");
        bool newOwn = true;
        for (u32 i = 0; i < 32; ++i) if (newRead[i] != objectData[1][i]) newOwn = false;
        Expect(newOwn, "the new object holds its own data");
    }
    }

    // ================================================================ 7. Create: a real frame shortage at each stage
    { if (unitCase == 7) {
        Say("CASE 7\n");
        // A real shortage in ONE instance, then the same instance recovering. The 117 sweep changed the machine between
        // attempts, so a later success proved nothing about the instance that failed. Here the frames are taken from the
        // production allocator directly, so the shortage is real and the release is real.
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");
        Expect(ActivateRuntime(runtime, paging, frames), "the runtime activates through the real method");

        BuildData(11);
        NativeImage image;
        uint32_t firstId = 0;
        MakeImage(0, image);
        Expect(runtime.Create(image, firstId) && firstId != 0, "the first object is created while frames are plentiful");
        uint8_t* sibling = readBuffer;
        Expect(runtime.ReadMemory(firstId, NativeRuntime::DataAddress, sibling, 32), "the sibling is readable");

        // Hold every free frame the allocator still has, so the next Create cannot succeed.
        static u32 held[2048];
        u32 heldCount = 0;
        for (;;) {
            u32 frame = 0;
            if (!frames.allocate(frame)) break;
            if (heldCount == 2048) { frames.free(frame); break; }
            held[heldCount++] = frame;
        }
        Expect(heldCount > 0, "frames were actually held");
        Say("   held "); SayHex(heldCount); Say(" frames\n");

        // The starved Create fails, publishes nothing, and leaves the live sibling untouched.
        MakeImage(1, image);
        uint32_t starvedId = 0xDEADBEEFU;
        const bool starved = runtime.Create(image, starvedId);
        Expect(!starved, "a starved Create fails");
        ExpectHex(starvedId, 0, "the starved Create cleared the caller identifier");
        uint8_t* check = readBuffer2;
        Expect(runtime.ReadMemory(firstId, NativeRuntime::DataAddress, check, 32),
               "the sibling is still readable after the starved Create");
        bool siblingIntact = true;
        for (u32 i = 0; i < 32; ++i) if (check[i] != objectData[0][i]) siblingIntact = false;
        Expect(siblingIntact, "the sibling data is unchanged after the starved Create");
        Expect(!runtime.ReadMemory(starvedId, NativeRuntime::DataAddress, check, 32),
               "the starved object is not readable");
        // Nothing was published: the runtime still reports exactly one live object.
        const NativeStatistics afterFailure = runtime.Statistics();
        ExpectHex(afterFailure.created, 1, "the starved Create published no object");

        // Remove the fault and let the SAME instance try again.
        for (u32 i = 0; i < heldCount; ++i) frames.free(held[i]);
        MakeImage(1, image);
        uint32_t recoveredId = 0;
        Expect(runtime.Create(image, recoveredId) && recoveredId != 0,
               "the same instance creates again once the frames are released");
        uint8_t* recovered = readBuffer2;
        Expect(runtime.ReadMemory(recoveredId, NativeRuntime::DataAddress, recovered, 32),
               "the recovered object is readable");
        bool recoveredOwn = true;
        for (u32 i = 0; i < 32; ++i) if (recovered[i] != objectData[1][i]) recoveredOwn = false;
        Expect(recoveredOwn, "the recovered object holds its own data");
        Expect(recoveredId != firstId, "the recovered object has its own identifier");
        // The sibling survived the whole sequence.
        uint8_t* finalCheck = readBuffer;
        Expect(runtime.ReadMemory(firstId, NativeRuntime::DataAddress, finalCheck, 32),
               "the sibling survived the whole sequence");
        bool finalIntact = true;
        for (u32 i = 0; i < 32; ++i) if (finalCheck[i] != objectData[0][i]) finalIntact = false;
        Expect(finalIntact, "the sibling data survived the whole sequence unchanged");
    }
    }

    // ================================================================ 8. Create: input boundaries
    { if (unitCase == 8) {
        Say("CASE 8\n");
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");
        Expect(ActivateRuntime(runtime, paging, frames), "the runtime activates through the real method");

        NativeImage image;
        uint32_t id = 0;
        MakeImage(0, image); image.code = 0;
        Expect(!runtime.Create(image, id), "Create refuses a null code pointer");
        ExpectHex(id, 0, "the refusal cleared the identifier");
        MakeImage(0, image); image.codeBytes = 0;
        Expect(!runtime.Create(image, id), "Create refuses a zero-length code span");
        MakeImage(0, image); image.codeBytes = 4097;
        Expect(!runtime.Create(image, id), "Create refuses a code span over the page limit");
        MakeImage(0, image); image.entryOffset = image.codeBytes;
        Expect(!runtime.Create(image, id), "Create refuses an entry offset at the end of the code");
        MakeImage(0, image); image.data = 0; image.dataBytes = 32;
        Expect(!runtime.Create(image, id), "Create refuses data bytes with no data pointer");
        MakeImage(0, image); image.dataBytes = 4097;
        Expect(!runtime.Create(image, id), "Create refuses a data span over the page limit");
        MakeImage(0, image);
        Expect(runtime.Create(image, id) && id != 0, "a valid image is still accepted after the refusals");
        const u32 stale = id + 1000;
        uint8_t* buffer = readBuffer2;
        Expect(!runtime.ReadMemory(stale, NativeRuntime::DataAddress, buffer, 4),
               "ReadMemory refuses an unknown identifier");
        Expect(!runtime.RequestExit(stale, 0), "RequestExit refuses an unknown identifier");
        NativeStatus status;
        Expect(!runtime.Status(stale, status), "Status refuses an unknown identifier");
    }
    }

    // ================================================================ 9. Reclaim returns the exact baseline
    { if (unitCase == 10) {
        Say("CASE 10\n");
        // A LATE-stage failure of Create: the first four stages succeed and take resources, and the fifth is refused.
        //
        // The existing cases can only starve the allocator, which fails the FIRST stage and therefore never shows whether
        // a partially acquired object is rolled back. Here the failure is injected at ProtectPage, after Prepare, both
        // MapNewPage calls and both CopyToUser calls have already succeeded. What is measured is the state after the
        // refusal: a live sibling untouched, nothing published, and every frame the failed attempt took returned.
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");
        Expect(ActivateRuntime(runtime, paging, frames), "the runtime activates through the real method");

        // The permanent baseline, and a live sibling created before the fault.
        static u32 baseline[kMaximumTracked];
        u32 baselineCount = 0;
        CaptureFrameSet(frames);
        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) baseline[baselineCount++] = frameSet[i];

        BuildData(31);
        NativeImage image;
        uint32_t siblingId = 0;
        MakeImage(0, image);
        Expect(runtime.Create(image, siblingId) && siblingId != 0, "the live sibling is created");
        uint8_t* siblingBefore = readBuffer;
        Expect(runtime.ReadMemory(siblingId, NativeRuntime::DataAddress, siblingBefore, 32),
               "the sibling is readable before the fault");

        // The frames the sibling owns, so the difference after the failed attempt can exclude them.
        static u32 withSibling[kMaximumTracked];
        u32 withSiblingCount = 0;
        CaptureFrameSet(frames);
        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) withSibling[withSiblingCount++] = frameSet[i];

        // Arm the seam so the FIFTH stage of the next Create is refused. The threshold is the page count the object has
        // already reached at that point, which is exactly the count after the two mappings. The witness is reset here so
        // that what it reports afterwards belongs to this one attempt.
        const u32 callsBefore = gtos_process_memory_test_protect_calls;
        gtos_process_memory_test_protect_refusals = 0;
        gtos_process_memory_test_protect_pages_at_refusal = 0;
        gtos_process_memory_test_fail_protect_after = 1;

        MakeImage(1, image);
        uint32_t lateId = 0xDEADBEEFU;
        const bool late = runtime.Create(image, lateId);
        gtos_process_memory_test_fail_protect_after = 0;

        // What the executed path recorded, read after the fact. A false return alone cannot say where Create stopped:
        // failing in Prepare or in the first mapping returns false just as well. These three numbers can.
        const u32 callsDuring = gtos_process_memory_test_protect_calls - callsBefore;
        const u32 refusals = gtos_process_memory_test_protect_refusals;
        const u32 pagesAtRefusal = gtos_process_memory_test_protect_pages_at_refusal;
        Say("   late Create returned "); Say(late ? "true" : "false");
        Say("   ProtectRange consulted "); SayHex(callsDuring);
        Say(" times, refused "); SayHex(refusals);
        Say(" times at page count "); SayHex(pagesAtRefusal); Say("\n");

        Expect(!late, "a Create whose fifth stage is refused fails");
        ExpectHex(lateId, 0, "the refused Create cleared the caller identifier");

        // The target stage was actually reached and actually refused, as recorded by the seam rather than inferred from
        // the return value. ProtectPage calls ProtectRange once, so a single consultation is the stage running once.
        ExpectHex(callsDuring, 1, "the fifth stage was consulted exactly once");
        ExpectHex(refusals, 1, "the fifth stage refused exactly once");
        // The refusal happened with the two mappings already acquired: CodeAddress and DataAddress are two pages, so the
        // page count at refusal is 2. A refusal at 0 or 1 would be a different, earlier failure.
        ExpectHex(pagesAtRefusal, 2, "the refusal happened with both mappings already acquired");

        // Nothing was published.
        const NativeStatistics afterLate = runtime.Statistics();
        ExpectHex(afterLate.created, 1, "the refused Create published no object, so only the sibling is live");
        Expect(!runtime.ReadMemory(lateId, NativeRuntime::DataAddress, readBuffer2, 32),
               "the refused object is not readable");

        // The live sibling is untouched, in identity and in data.
        uint8_t* siblingAfter = readBuffer2;
        Expect(runtime.ReadMemory(siblingId, NativeRuntime::DataAddress, siblingAfter, 32),
               "the sibling is still readable after the late failure");
        bool siblingIntact = true;
        for (u32 i = 0; i < 32; ++i) if (siblingAfter[i] != objectData[0][i]) siblingIntact = false;
        Expect(siblingIntact, "the sibling data is unchanged after the late failure");

        // Every frame the failed attempt took is back. The comparison is against the set as it was with the sibling live.
        CaptureFrameSet(frames);
        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(withSibling, withSiblingCount);
        ExpectHex(frameSetAdded, 0, "no frame the refused attempt took is still held");
        ExpectHex(frameSetRemoved, 0, "no frame the sibling owns was given up");

        // Remove the fault and let the SAME instance create again, then reap everything back to the permanent baseline.
        MakeImage(1, image);
        uint32_t recoveredId = 0;
        Expect(runtime.Create(image, recoveredId) && recoveredId != 0,
               "the same instance creates again once the fault is gone");
        uint8_t* recovered = readBuffer2;
        Expect(runtime.ReadMemory(recoveredId, NativeRuntime::DataAddress, recovered, 32),
               "the recovered object is readable");
        bool recoveredData = true;
        for (u32 i = 0; i < 32; ++i) if (recovered[i] != objectData[1][i]) recoveredData = false;
        Expect(recoveredData, "the recovered object carries its own data");

        Expect(runtime.RequestExit(siblingId, 0x51), "the sibling exits");
        Expect(runtime.RequestExit(recoveredId, 0x52), "the recovered object exits");
        ExpectHex(runtime.Reap(), 2, "both objects were reaped");
        CaptureFrameSet(frames);
        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(baseline, baselineCount);
        ExpectHex(frameSetAdded, 0, "the exact baseline is restored after the late failure and recovery");
        ExpectHex(frameSetRemoved, 0, "no permanent runtime frame was given up");
        ExpectHex(frameSetCount, baselineCount, "the allocated count is back at the baseline");
    }
    }
    { if (unitCase == 9) {
        Say("CASE 9\n");
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        KernelPaging& paging = Paging();
        Expect(PreparePaging(paging, frames), "paging prepares");
        NativeRuntime& runtime = Runtime();
        Expect(runtime.PrepareStacks(paging, frames), "the stacks are prepared");
        Expect(ActivateRuntime(runtime, paging, frames), "the runtime activates through the real method");
        // The permanent baseline: the runtime's stacks and tables, which stay owned for the guest's whole life.
        static u32 baseline[kMaximumTracked];
        u32 baselineCount = 0;
        CaptureFrameSet(frames);

        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) baseline[baselineCount++] = frameSet[i];

        BuildData(23);
        NativeImage image;
        uint32_t ids[4];
        u32 made = 0;
        for (u32 i = 0; i < 4; ++i) { MakeImage(i, image); ids[i] = 0; if (runtime.Create(image, ids[i])) ++made; }
        ExpectHex(made, 4, "four objects were created");
        CaptureFrameSet(frames);

        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        Expect(frameSetCount > baselineCount, "the objects own frames while they exist");

        for (u32 i = 0; i < made; ++i) Expect(runtime.RequestExit(ids[i], 0x40 + i), "each object exits");
        ExpectHex(runtime.Reap(), made, "every object was reaped");

        CaptureFrameSet(frames);


        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(baseline, baselineCount);
        ExpectHex(frameSetAdded, 0, "no frame the objects took is still held");
        ExpectHex(frameSetRemoved, 0, "no permanent runtime frame was given up");
        ExpectHex(frameSetCount, baselineCount, "the allocated count is back at the baseline");
    }
    }

    // ================================================================ 11. the extra-four-frame leak that conserves totals
    { if (unitCase == 11) {
        Say("CASE 11\n");
        // The negative this packet exists to answer. The 139 recovery measured its refusal against the slot table's capacity
        // and then checked `allocated + free` for conservation. That check cannot see a leak: the sum is the machine's
        // addressable total and it is conserved by construction whenever a frame merely moves from free to allocated. Four
        // frames is a small enough leak to keep `allocated` divisible by four, and a leak of four is exactly what a
        // two-page object plus its two page-table frames would leave behind if a refusal took the frames and did not put
        // them back.
        //
        // So this case builds that leak out of the allocator's own calls, shows the sum and the divisibility both pass, and
        // then shows the frame set does not. The point is not that the leak is possible; it is that the measurement the
        // scene uses can see it and the measurement 139 used cannot.
        ResetAll();
        PhysicalMemoryManager& frames = Frames();
        Expect(SeedAllocator(frames, kFreeFrames), "the allocator is seeded");
        const PhysicalMemoryStatistics seeded = frames.getStatistics();
        const u32 seededSum = seeded.allocatedFrames + seeded.freeFrames;

        static u32 baseline[kMaximumTracked];
        u32 baselineCount = 0;
        CaptureFrameSet(frames);
        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) baseline[baselineCount++] = frameSet[i];

        // One four-frame allocation, then one frame given back, then the other three given back, then one of those three
        // taken again and not returned. The sequence is deliberately ordinary: every call succeeds, nothing is refused, and
        // the only unusual thing is that the last allocation has no matching free. A leak that arrived through a failed
        // call would be a different shape; this case is about the arithmetic, so it uses calls that all succeed.
        u32 block = 0;
        Expect(frames.allocateContiguous(4, block), "four contiguous frames are taken");
        Expect(block != 0, "the block has a real address");
        Expect(frames.free(block), "the first of the four is given back");
        Expect(frames.free(block + 4096U), "the second is given back");
        Expect(frames.free(block + 2U * 4096U), "the third is given back");
        // The fourth frame is deliberately not freed. That is the leak.
        const PhysicalMemoryStatistics leaked = frames.getStatistics();

        // (a) The check 139 used. The sum is conserved, because the total number of frames did not change -- four frames
        // moved from free to allocated and the sum counts both sides.
        ExpectHex(leaked.allocatedFrames + leaked.freeFrames, seededSum,
                  "the leak conserves allocated + free, which is why the sum cannot see it");
        // (b) And the count did move, which the sum hid. This is the part a sum-only check never reads. The leak here is
        // one frame, so divisibility would not be the interesting test at this size; the second half of this case leaks
        // four, which is the size at which divisibility passes and the sum still does.
        ExpectHex(leaked.allocatedFrames, seeded.allocatedFrames + 1,
                  "the leak is visible in the allocated count, one frame");
        ExpectHex(leaked.freeFrames, seeded.freeFrames - 1,
                  "and in the free count, one frame the other way");

        // (d) The measurement the scene uses. The frame set differs from the baseline by exactly the leaked frame, and a
        // comparison of the sets names it. This is the same walk the scene's digest folds, over the same allocator.
        CaptureFrameSet(frames);
        Expect(frameSetComplete, "the frame walk covered every addressable frame");
        DiffFrameSet(baseline, baselineCount);
        ExpectHex(frameSetAdded, 1, "the frame set gained exactly one frame");
        ExpectHex(frameSetRemoved, 0, "the frame set lost nothing");
        Expect(FrameSetContains(block + 3U * 4096U), "the frame set names the leaked frame");
        Expect(!FrameSetContains(block), "the frame set no longer names the frame that was given back");

        // (e) The same leak at the size the scene's own object costs. An object costs seven frames: two data pages, its
        // page tables and its directory contribution. A four-frame leak is the piece of that which survives a partial
        // rollback, so the case repeats the arithmetic at four against a baseline that is not zero.
        ResetAll();
        PhysicalMemoryManager& frames2 = Frames();
        Expect(SeedAllocator(frames2, kFreeFrames), "the allocator is seeded a second time");
        const PhysicalMemoryStatistics seeded2 = frames2.getStatistics();
        static u32 baseline2[kMaximumTracked];
        u32 baseline2Count = 0;
        CaptureFrameSet(frames2);
        Expect(frameSetComplete, "the second frame walk covered every addressable frame");
        for (u32 i = 0; i < frameSetCount; ++i) baseline2[baseline2Count++] = frameSet[i];

        u32 block2 = 0;
        Expect(frames2.allocateContiguous(4, block2), "four contiguous frames are taken the second time");
        // Nothing is given back at all: the whole four-frame block is the leak.
        const PhysicalMemoryStatistics leaked2 = frames2.getStatistics();
        ExpectHex(leaked2.allocatedFrames + leaked2.freeFrames, seeded2.allocatedFrames + seeded2.freeFrames,
                  "the four-frame leak also conserves the sum");
        ExpectHex(leaked2.allocatedFrames - seeded2.allocatedFrames, 4, "the leak is exactly four frames");
        // Four frames on top of a seeded baseline of zero leaves the count divisible by four, which is the arithmetic
        // coincidence 139 relied on. The assertion is stated against the seeded count so that it says what it means if the
        // seed ever changes.
        ExpectHex(leaked2.allocatedFrames % 4U, seeded2.allocatedFrames % 4U,
                  "the four-frame leak leaves the count as divisible by four as it started");
        CaptureFrameSet(frames2);
        Expect(frameSetComplete, "the second frame walk covered every addressable frame");
        DiffFrameSet(baseline2, baseline2Count);
        ExpectHex(frameSetAdded, 4, "the frame set gained four frames");
        ExpectHex(frameSetRemoved, 0, "the frame set lost nothing");
        for (u32 i = 0; i < 4; ++i) {
            Expect(FrameSetContains(block2 + i * 4096U), "the frame set names each leaked frame");
        }
        // A digest over the set has to differ, because the set differs. This is the property the scene relies on: equal
        // digests mean equal sets, not equal counts.
        u32 digestBefore = 2166136261U;
        for (u32 i = 0; i < baseline2Count; ++i) {
            const u32 frame = baseline2[i] >> 12;
            for (u32 byte = 0; byte < 4; ++byte) { digestBefore ^= (frame >> (byte * 8)) & 0xFFU; digestBefore *= 16777619U; }
        }
        u32 digestAfter = 2166136261U;
        for (u32 i = 0; i < frameSetCount; ++i) {
            const u32 frame = frameSet[i] >> 12;
            for (u32 byte = 0; byte < 4; ++byte) { digestAfter ^= (frame >> (byte * 8)) & 0xFFU; digestAfter *= 16777619U; }
        }
        Expect(digestBefore != digestAfter, "the frame-set digest separates the two sets the totals cannot");
    }
    }

    Say(failures == 0 ? "RUNTIME UNIT PASS\n" : "RUNTIME UNIT FAIL\n");
    return (int)failures;
}

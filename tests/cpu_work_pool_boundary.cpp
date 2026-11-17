// GTOS-DSH-115 S0: the machine boundary the CpuWorkPool host unit stands on.
//
// 113 shipped this file with every boundary function hard-wired to "not ready", so Prepare() and Start() could never be
// entered and the unit quietly installed its own shared block instead. The 114 review named that precisely: a unit that
// never calls the method under test does not test it, and a group that only exercises an empty object is not a Start
// regression.
//
// This file therefore answers questions and decides nothing:
//
//   allocation      how much contiguous memory exists, and whether this allocation fails
//   startup         how many processors were detected, which have stacks, whether delivery succeeds
//   memory types    the MTRR/PAT snapshot, which a real BSP reads from MSRs
//   paging          whether a directory exists, whether it is sealed, whether a range is mapped
//
// The production Prepare(), Start(), Submit(), Collect(), CloseAdmission(), IsDrained(), RequestPark() and
// ObservePark() all run for real against those answers. There is no shared-block construction here, no worker record, no
// admission decision, no drain arithmetic and no park state: if the pool is bypassed, the test fails instead of passing
// on something fabricated.

#include <common/types.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_memory_types.h>
#include <memory/bootstrap.h>
#include <memory/physical.h>
#include <memory/paging.h>

using namespace gtos::hardwarecommunication;

// The linker symbols a real image provides. Distinct values so a range test is meaningful; the unit never walks one.
// A real image's linker script places these in a fixed order, and Start() relies on that: it refuses when the read-only
// range is empty. Two loose objects in .bss have no guaranteed order, so the boundary puts them in sections the unit's
// script orders explicitly, which is the same guarantee the production script gives.
extern "C" {
__attribute__((section(".gtos_kernel"))) uint8_t kernel_start[1] = {0};
__attribute__((section(".gtos_kernel"))) uint8_t kernel_end[1] = {0};
__attribute__((section(".gtos_ro_start"))) uint8_t kernel_readonly_start[1] = {0};
__attribute__((section(".gtos_ro_end"))) uint8_t kernel_readonly_end[1] = {0};
}

// ---------------------------------------------------------------- the machine's answers
// Every one of these is written by the test before it calls a production method, and read by the boundary below.
extern "C" {
// Startup: how the firmware inventory looks and whether delivery works.
uint32_t gtos_host_machine_processors = 0;
uint32_t gtos_host_machine_local_apic = 0xFEE00000U;
uint32_t gtos_host_machine_ready = 0;
uint32_t gtos_host_machine_delivery = 1;
uint32_t gtos_host_machine_error = 0;
uint32_t gtos_host_machine_attempted_aps = 0;
uint32_t gtos_host_machine_start_calls = 0;
uint32_t gtos_host_machine_processor_apic[8];
uint32_t gtos_host_machine_processor_state[8];
uint32_t gtos_host_machine_processor_stack_base[8];
uint32_t gtos_host_machine_processor_stack_pages[8];

// Allocation: a simple arena the test can exhaust or make fail on demand.
// A real host buffer, so the addresses the allocator returns are dereferenceable. The pool zeroes the block it is given
// and places worker records inside it, which on a host has to mean real memory rather than a physical number.
uint8_t* gtos_host_machine_arena = 0;
uint32_t gtos_host_machine_arena_pages = 256;
uint32_t gtos_host_machine_arena_used = 0;
uint32_t gtos_host_machine_allocation_fail = 0;
uint32_t gtos_host_machine_allocation_calls = 0;

// The arena and the directory are host memory. Initialised once, before any case runs; the used counter is what a case
// resets. Nothing frees this storage, which is the same statement a real allocator makes about a retained block.
__attribute__((aligned(4096))) uint8_t gtos_host_machine_arena_storage[256 * 4096];

// Memory types: whether the MTRR/PAT snapshot can be captured at all.
uint32_t gtos_host_machine_memory_types = 1;

// Paging: whether a directory exists and whether sealing succeeds.
uint32_t gtos_host_machine_paging_prepared = 1;
// Start() walks the directory as memory, so it is a real zeroed array too.
__attribute__((aligned(4096))) uint32_t gtos_host_machine_paging_directory_storage[1024];
uint32_t gtos_host_machine_paging_directory = 0;
uint32_t gtos_host_machine_paging_seal = 1;
uint32_t gtos_host_machine_paging_sealed = 0;
}

extern "C" void gtos_host_machine_init() {
    gtos_host_machine_arena = gtos_host_machine_arena_storage;
    gtos_host_machine_paging_directory = (uint32_t)gtos_host_machine_paging_directory_storage;
    for (uint32_t i = 0; i < 1024; ++i) gtos_host_machine_paging_directory_storage[i] = 0;
    gtos_host_machine_arena_used = 0;
}


namespace gtos { namespace hardwarecommunication {

namespace {
void Unreachable(const char* what) {
    (void)what;
    for (;;) asm volatile("cli; hlt" : : : "memory");
}
} // namespace

extern "C" bool gtos_host_ap_enter(uint32_t apicId);

// ---------------------------------------------------------------- the startup protocol
// The pool prepares and starts a real CpuStartup. These answer what the machine would answer, so the production code
// decides what that means. A failed delivery is a real case: Start() used to publish "started" before calling
// StartPrepared, so a failure left a pool that claimed to have started.

CpuStartupReport CpuStartup::GetReport() const {
    CpuStartupReport answer;
    answer.error = (CpuStartupError)gtos_host_machine_error;
    answer.detectedProcessors = gtos_host_machine_processors;
    answer.attemptedAps = gtos_host_machine_attempted_aps;
    answer.acknowledgedAps = 0;
    answer.initializedAps = 0;
    answer.parkedAps = 0;
    answer.failedAps = 0;
    answer.schedulerOnlineProcessors = 0;
    answer.trampolineAddress = 0x8000;
    answer.localApicAddress = gtos_host_machine_local_apic;
    return answer;
}
bool CpuStartup::ReadyToStart() const {
    return gtos_host_machine_ready != 0;
}
bool CpuStartup::GetProcessor(uint32_t index, CpuStartupProcessorInfo& result) const {
    if (index >= gtos_host_machine_processors || index >= 8) return false;
    result.apicId = gtos_host_machine_processor_apic[index];
    result.state = (CpuStartupState)gtos_host_machine_processor_state[index];
    result.stackBase = gtos_host_machine_processor_stack_base[index];
    result.stackPages = gtos_host_machine_processor_stack_pages[index];
    result.observedApicId = result.apicId;
    result.observedStackPointer = result.stackBase + result.stackPages * 4096;
    result.selfTestChecksum = 0;
    return true;
}
bool CpuStartup::StartPrepared(CpuStartupContinuation continuation, void* context) {
    (void)continuation;
    (void)context;
    ++gtos_host_machine_start_calls;
    if (!gtos_host_machine_delivery) return false;
    // Delivery succeeded, so each processor whose self test passed reaches its idle publication. This is the AP-side half
    // of the startup protocol the seam stands for; the pool's readiness accounting is untouched.
    for (uint32_t i = 1; i < gtos_host_machine_processors && i < 8; ++i) {
        if (gtos_host_machine_processor_state[i] == CpuStartupSelfTestFailed) continue;
        gtos_host_ap_enter(gtos_host_machine_processor_apic[i]);
    }
    return true;
}
bool CpuStartup::GetSharedMemoryRange(uint32_t& address, uint32_t& bytes) const {
    // A real trampoline lives below 1 MiB. The value only has to be a nonzero range for Start()'s mapping gate.
    address = 0x8000;
    bytes = 0x1000;
    return true;
}
const char* CpuStartup::ErrorName(CpuStartupError error) {
    switch (error) {
    case CpuStartupOk: return "ok";
    case CpuStartupAlreadyRun: return "already-run";
    case CpuStartupInterruptsEnabled: return "interrupts-enabled";
    case CpuStartupPagingEnabled: return "paging-enabled";
    case CpuStartupNoFirmware: return "no-firmware";
    case CpuStartupInvalidFirmware: return "invalid-firmware";
    case CpuStartupNoLocalApic: return "no-local-apic";
    case CpuStartupX2ApicUnsupported: return "x2apic-unsupported";
    case CpuStartupBadApicBase: return "bad-apic-base";
    case CpuStartupNoTrampoline: return "no-trampoline";
    case CpuStartupBadTrampoline: return "bad-trampoline";
    case CpuStartupNoSharedMemory: return "no-shared-memory";
    case CpuStartupTimerUnavailable: return "timer-unavailable";
    case CpuStartupPartialFailure: return "partial-failure";
    case CpuStartupNotPrepared: return "not-prepared";
    }
    return "unknown";
}
bool CpuStartup::ReadFirmwareInventory(const CpuInfo&, uint32_t, uint32_t* apicIds, uint32_t capacity,
                                       uint32_t& count, uint32_t& localApicAddress) {
    if (gtos_host_machine_processors > capacity || gtos_host_machine_processors > 8) { count = 0; return false; }
    for (uint32_t i = 0; i < gtos_host_machine_processors; ++i) apicIds[i] = gtos_host_machine_processor_apic[i];
    count = gtos_host_machine_processors;
    localApicAddress = gtos_host_machine_local_apic;
    return true;
}

// ---------------------------------------------------------------- the memory-type boundary
// A real BSP reads MSRs here. The host cannot, so the test says whether the snapshot is available.

bool CaptureMemoryTypes(CpuMemoryTypes& result) {
    if (!gtos_host_machine_memory_types) return false;
    result.valid = 1;
    result.cpuidFeatures = 0;
    result.physicalAddressBits = 32;
    result.variableCount = 0;
    result.capability = 0;
    result.defaultType = 6; // write-back
    result.pat = 0x0007040600070406ULL;
    for (uint32_t i = 0; i < CpuMemoryTypes::MaximumVariableMtrrs; ++i) {
        result.variableBase[i] = 0;
        result.variableMask[i] = 0;
    }
    for (uint32_t i = 0; i < CpuMemoryTypes::FixedMtrrCount; ++i) result.fixed[i] = 0;
    return true;
}
bool MemoryTypeRangeIsWriteBack(const CpuMemoryTypes& types, uint32_t, uint32_t length) {
    return types.valid != 0 && length != 0;
}
bool PrepareApplicationProcessorMemory(const CpuMemoryTypes& bootstrap,CpuMemoryTypeFailure*,bool) {
    return bootstrap.valid != 0;
}

} }


// ---------------------------------------------------------------- construction
// The pool places worker records inside the block it allocated, so it needs placement new. There is no C++ runtime in
// this unit, which is the same reason a real image supplies these itself.

void* operator new(unsigned int, void* where) { return where; }
void operator delete(void*, void*) {}

namespace gtos { namespace hardwarecommunication {

// The pool holds a CpuStartup by reference and calls Prepare/Start on it. Constructing one has to be possible; every
// answer it gives comes from the machine globals above.
CpuStartup::CpuStartup()
    : shared(0), used(false), prepared(false), started(false) {
    report.error = CpuStartupOk;
    report.detectedProcessors = 0;
    report.attemptedAps = 0;
    report.acknowledgedAps = 0;
    report.initializedAps = 0;
    report.parkedAps = 0;
    report.failedAps = 0;
    report.schedulerOnlineProcessors = 0;
    report.trampolineAddress = 0;
    report.localApicAddress = 0;
}

} }

namespace gtos { namespace memory {

// The pool takes the allocator by reference and never uses any method but allocateContiguous, which is answered above.
// Construction must still work, so the manager starts in a clean uninitialized state.
PhysicalMemoryManager::PhysicalMemoryManager() { clear(); }
// clear() is private and the unit never allocates a real frame, so the manager's own reset is defined here rather than
// invented: the fields are set to the same "nothing known yet" state the constructor above wants.
void PhysicalMemoryManager::clear() {
    frameCount = 0;
    freeCount = 0;
    allocatedCount = 0;
    failures = 0;
    ready = false;
    mapUsed = false;
    error = PhysicalMemoryOk;
    for (uint32_t i = 0; i < MaximumFrames / 32; ++i) { eligible[i] = 0; allocated[i] = 0; }
}

// The pool asks for statistics, the seal and a byte query. Construction must work for the same reason.
KernelPaging::KernelPaging()
    : frames(0), directory(0), mappedCount(0), tableCount(0),
      prepared(false), active(false), sharedSealed(false), error(PagingOk) {
    for (uint32_t i = 0; i < 1024; ++i) tables[i] = 0;
}

} }

namespace gtos { namespace memory {

namespace {
void UnreachablePool() {
    for (;;) asm volatile("cli; hlt" : : : "memory");
}
} // namespace

// ---------------------------------------------------------------- the boot-only SIPI pool
LowBootstrapPool::LowBootstrapPool() {}
void LowBootstrapPool::clear() {}
void LowBootstrapPool::addAvailable(uint64_t, uint64_t) { UnreachablePool(); }
void LowBootstrapPool::reserve(uint64_t, uint64_t) { UnreachablePool(); }
bool LowBootstrapPool::restrictFirmware(uint32_t, uint32_t, uint32_t) { return false; }
bool LowBootstrapPool::restrictEfiMemoryMap() { return false; }
bool LowBootstrapPool::claim(uint32_t&) { return false; }
bool LowBootstrapPool::intersectsClaimed(uint64_t, uint64_t) const { return false; }
bool LowBootstrapPool::isClaimed(uint32_t) const { return false; }
uint32_t LowBootstrapPool::freePages() const { return 0; }
uint32_t LowBootstrapPool::claimedPages() const { return 0; }
void LowBootstrapPool::range(uint64_t, uint64_t, uint32_t& first, uint32_t& last) const {
    first = 0;
    last = 0;
}

// ---------------------------------------------------------------- the allocator boundary
// The pool asks for one contiguous block and never frees it. The arena answers whether that request fits, and can be
// told to fail so the production failure path is reachable.

bool PhysicalMemoryManager::allocateContiguous(uint32_t pages, uint32_t& address,
                                               uint32_t alignmentPages, uint32_t maxAddress) {
    ++gtos_host_machine_allocation_calls;
    address = 0;
    if (gtos_host_machine_allocation_fail) return false;
    if (!pages || alignmentPages == 0) return false;
    if (!gtos_host_machine_arena) return false;
    const uint32_t aligned = (gtos_host_machine_arena_used + alignmentPages - 1)
        / alignmentPages * alignmentPages;
    const uint32_t next = aligned + pages;
    if (next > gtos_host_machine_arena_pages) return false;
    const uint32_t base = (uint32_t)gtos_host_machine_arena;
    if ((uint64_t)base + (uint64_t)next * 4096 > (uint64_t)maxAddress + 1) return false;
    gtos_host_machine_arena_used = next;
    address = base + aligned * 4096;
    return true;
}

// ---------------------------------------------------------------- the paging boundary
// The unit does not build page tables. These report whether a directory exists and whether the freeze succeeded, which is
// what Start()'s gate asks.

PagingStatistics KernelPaging::getStatistics() const {
    PagingStatistics answer;
    answer.mappedPages = 0;
    answer.pageTableFrames = 0;
    answer.directoryAddress = gtos_host_machine_paging_prepared
        ? (uint32_t)gtos_host_machine_paging_directory_storage : 0;
    answer.prepared = gtos_host_machine_paging_prepared != 0;
    answer.enabled = false;
    answer.sealedForSharing = gtos_host_machine_paging_sealed != 0;
    answer.pae = false;
    return answer;
}
bool KernelPaging::query(uint32_t virtualAddress, PagingMapping& result) const {
    result.physicalAddress = 0;
    result.writable = false;
    result.userAccessible = false;
    result.cacheDisabled = false;
    result.managed = false;
    if (!gtos_host_machine_paging_prepared) return false;
    // The identity window this boundary models: the arena the allocator hands out, the trampoline below 1 MiB, and the
    // directory itself. Everything else is a hole, so Start()'s gate still discriminates.
    const uint32_t arenaStart = (uint32_t)gtos_host_machine_arena;
    const uint32_t arenaEnd = arenaStart + gtos_host_machine_arena_pages * 4096;
    const bool inArena = virtualAddress >= arenaStart && virtualAddress < arenaEnd;
    const bool inTrampoline = virtualAddress >= 0x8000 && virtualAddress < 0x9000;
    const uint32_t directory = (uint32_t)gtos_host_machine_paging_directory_storage;
    const bool isDirectory = virtualAddress >= directory && virtualAddress < directory + 0x1000;
    if (!inArena && !inTrampoline && !isDirectory) return false;
    result.physicalAddress = virtualAddress;
    result.writable = true;
    result.managed = inArena;
    return true;
}
bool KernelPaging::sealForSharedProcessors(uint32_t* workerDirectory) {
    if (workerDirectory) *workerDirectory = 0;
    if (!gtos_host_machine_paging_seal) return false;
    gtos_host_machine_paging_sealed = 1;
    if (workerDirectory) *workerDirectory = gtos_host_machine_paging_directory;
    return true;
}

} }

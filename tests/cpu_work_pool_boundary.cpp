// GTOS-DSH-113 G1: the hardware and allocation boundary the CpuWorkPool host unit needs in order to link.
//
// This file is NOT a substitute for the pool. Every symbol below belongs to a layer the unit deliberately does not
// exercise, and each one is named with the boundary it stands for:
//
//   kernel_start / kernel_end / kernel_readonly_start / kernel_readonly_end
//       linker-provided symbols in a real image. The host unit is not the kernel, so they are defined here.
//   CpuStartup::GetReport / ReadyToStart / GetProcessor / StartPrepared / GetSharedMemoryRange
//       the AP bring-up protocol. Prepare() and Start() are never called by this unit, so these exist only so the
//       linker can resolve code paths the unit does not enter.
//   LowBootstrapPool
//       the boot-only SIPI page pool. Prepare() would allocate from it; the unit installs its shared block instead.
//
// What is emphatically NOT here: CpuWorkQueue in any form, or CpuWorkPool::Submit, Collect, CloseAdmission, IsDrained,
// ParkWorkers, AdmissionClosed or SubmittedJobs. Those are the real production code under test, and stubbing them
// would make the unit meaningless.
//
// Anything that would be a lie if a test reached it halts, so a unit that accidentally depends on this file fails
// loudly rather than passing on fabricated behaviour.

#include <common/types.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_memory_types.h>
#include <memory/bootstrap.h>

using namespace gtos::hardwarecommunication;

// The linker symbols a real image provides. Distinct zero values are enough: the unit never walks a kernel range.
extern "C" {
uint8_t kernel_start = 0;
uint8_t kernel_end = 0;
uint8_t kernel_readonly_start = 0;
uint8_t kernel_readonly_end = 0;
}

namespace gtos { namespace hardwarecommunication {

namespace {
// Reached only if a unit enters a path this file is not allowed to fake.
void Unreachable(const char* what) {
    (void)what;
    for (;;) asm volatile("cli; hlt" : : : "memory");
}
}

// The AP bring-up protocol. The unit never calls Prepare or Start.
CpuStartupReport CpuStartup::GetReport() const { return report; }
bool CpuStartup::ReadyToStart() const { return false; }
bool CpuStartup::GetProcessor(uint32_t, CpuStartupProcessorInfo&) const { return false; }
bool CpuStartup::StartPrepared(CpuStartupContinuation, void*) { return false; }
bool CpuStartup::GetSharedMemoryRange(uint32_t&, uint32_t&) const { return false; }

} }

namespace gtos { namespace memory {

namespace {
void UnreachablePool() {
    for (;;) asm volatile("cli; hlt" : : : "memory");
}
}

// The boot-only SIPI pool. Declared here so the linker is satisfied; any real use halts.
LowBootstrapPool::LowBootstrapPool() {}
void LowBootstrapPool::clear() {}
void LowBootstrapPool::addAvailable(uint64_t, uint64_t) { UnreachablePool(); }
void LowBootstrapPool::reserve(uint64_t, uint64_t) { UnreachablePool(); }
bool LowBootstrapPool::restrictFirmware(uint32_t, uint32_t, uint32_t) { return false; }
bool LowBootstrapPool::claim(uint32_t&) { return false; }
bool LowBootstrapPool::intersectsClaimed(uint64_t, uint64_t) const { return false; }
bool LowBootstrapPool::isClaimed(uint32_t) const { return false; }
uint32_t LowBootstrapPool::freePages() const { return 0; }
uint32_t LowBootstrapPool::claimedPages() const { return 0; }
void LowBootstrapPool::range(uint64_t, uint64_t, uint32_t& first, uint32_t& last) const {
    first = 0;
    last = 0;
}

} }

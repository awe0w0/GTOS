// GTOS-DSH-116 L1: the boundary the NativeRuntime host unit stands on.
//
// The 115 review named the gap: PrepareStacks and Activate had only a guest positive initialisation, with no unit of
// their own, so their resource failure paths and their guard and ownership obligations were never measured. This file
// supplies what a host cannot supply and decides nothing:
//
//   the bootstrap-processor identity    a CPUID and an APIC-base MSR read on a real machine
//   CR3                                 a control-register read
//   fast-entry disabling                an MSR write
//   interrupt masking                   CLI/STI, already seamed by the existing GTOS_MEMORY_TEST macro
//   the memory the firmware describes   an addressable-frame count and a free range
//
// Everything the runtime and the allocator decide is the production code: the frame bitmap, the free count, the page
// table construction, the stack layout, the guard windows, the rollback on failure and the state the object publishes.
// A test that installed a stack or marked a page mapped here would be writing the answer it is supposed to measure.

#include <common/types.h>
#include <memory/physical.h>
#include <memory/paging.h>
#include <process/native_runtime.h>
#include <memory/bootstrap.h>
#include <hardwarecommunication/port.h>

using namespace gtos::memory;

// The linker symbols a real image provides. Placed in ordered sections so the read-only range is never empty, which is the
// same guarantee the production linker script gives.
// The memory the machine has. Placed at a fixed low address by the unit's linker script, so a frame number the allocator
// returns is also the address of the frame: that is what an identity map gives the kernel, and it is what makes the
// production allocator and page-table code work unmodified here.
extern "C" {
__attribute__((section(".gtos_arena"), aligned(4096))) uint8_t gtos_host_arena[8 * 1024 * 1024] = {0};
__attribute__((section(".gtos_kernel"))) uint8_t kernel_start[1] = {0};
__attribute__((section(".gtos_kernel"))) uint8_t kernel_end[1] = {0};
__attribute__((section(".gtos_ro_start"))) uint8_t kernel_readonly_start[1] = {0};
__attribute__((section(".gtos_ro_end"))) uint8_t kernel_readonly_end[1] = {0};
}

// ---------------------------------------------------------------- the machine's answers
extern "C" {
// Whether this processor is the bootstrap one, whether CR3 currently names the shared directory, and whether the
// fast-entry MSRs could be cleared. All three are answers, not decisions.
uint32_t gtos_host_runtime_bsp = 1;
uint32_t gtos_host_runtime_cr3_value = 0;
uint32_t gtos_host_runtime_fast_entry_ok = 1;

// Counters, so a test can show the runtime really asked.
uint32_t gtos_host_runtime_cr3_reads = 0;
uint32_t gtos_host_runtime_fast_entry_calls = 0;
}

extern "C" uint32_t gtos_host_runtime_is_bsp() { return gtos_host_runtime_bsp; }
extern "C" uint32_t gtos_host_runtime_cr3() {
    ++gtos_host_runtime_cr3_reads;
    return gtos_host_runtime_cr3_value;
}
extern "C" uint32_t gtos_host_runtime_fast_entry() {
    ++gtos_host_runtime_fast_entry_calls;
    return gtos_host_runtime_fast_entry_ok;
}

// ---------------------------------------------------------------- what the unit must never reach
//
// The production enable() loads CR3 and sets CR0, which a host cannot do. It is left exactly as production wrote it: the
// unit never enables paging, because Activate's own preconditions are what is under test and they are checked before any
// control register is touched. If a test ever reached enable() the host would fault, which is the honest signal that the
// test took a path it was not written for -- rather than a stub quietly reporting success.

// ---------------------------------------------------------------- layers this unit does not exercise
//
// The console writers and the boot-only SIPI pool belong to layers a host unit deliberately does not reach. They exist
// here so the linker is satisfied. Anything that would be a lie if a unit reached it halts, so an accidental dependency
// fails loudly rather than passing on fabricated behaviour.
// native_runtime.cpp declares these two at file scope with C++ linkage, so they are defined the same way here.
void printf(char* text) { (void)text; for (;;) asm volatile("cli; hlt" : : : "memory"); }
void printfHex32(uint32_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }

// The port layer executes IN/OUT, which a host cannot do. Activate refuses before it ever samples the realtime clock, so
// these are never reached; reaching one halts rather than answering, which is the honest signal.
namespace gtos { namespace hardwarecommunication {

Port::Port(uint16_t port) : portnumber(port) {}
Port::~Port() {}
Port8Bit::Port8Bit(uint16_t port) : Port(port) {}
Port8Bit::~Port8Bit() {}
uint8_t Port8Bit::Read() { for (;;) asm volatile("cli; hlt" : : : "memory"); }
void Port8Bit::Write(uint8_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }

} }

namespace gtos { namespace memory {

LowBootstrapPool::LowBootstrapPool() {}
void LowBootstrapPool::clear() {}
void LowBootstrapPool::addAvailable(uint64_t, uint64_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }
void LowBootstrapPool::reserve(uint64_t, uint64_t) { for (;;) asm volatile("cli; hlt" : : : "memory"); }
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

// GTOS-DSH-138 H: the host machine boundary for the GDT observation, in one place.
//
// gdt.cpp's observation code is production code. What a host cannot do is execute STR or SGDT, or read a descriptor table
// without faulting. Those three machine-level reads are supplied through these symbols, exactly as the runtime already
// takes CR0 and CR3 from its own boundary.
//
// The counters are not decoration. Because the host cannot fault, a read outside the declared window is *recorded* rather
// than trapped, and the window test lives in gdt.cpp's SafeReadByte, not here. That is what lets a refusal be proved by a
// counter: a case that must not touch the task state can assert that no task-state byte was charged, even when the address
// it refused was a legal one inside the window.
//
// In 137 these nine definitions lived inside safe_tss_test.cpp, so only that one unit could link gdt.cpp. The runtime unit
// links gdt.cpp too and could not, which is the nine-symbol gap this packet closes. Moving the definitions here means one
// definition for every unit that links the observation, and no unit gets to invent its own.

#ifndef GTOS_TESTS_GDT_HOST_SEAM_H
#define GTOS_TESTS_GDT_HOST_SEAM_H

#include <common/types.h>

namespace gtos {
namespace testseam {

// What a machine would have returned from STR and SGDT.
extern "C" uint32_t gtos_gdt_host_str_selector;
extern "C" uint32_t gtos_gdt_host_sgdt_base;
extern "C" uint32_t gtos_gdt_host_sgdt_limit;
extern "C" uint32_t gtos_gdt_host_reads;

// Whether the modelled machine executed `ltr`.
//
// This is not the same fact as "LoadTaskState was called", and the difference is what packet 139 was about. A host process
// cannot execute `ltr`, so when a host unit calls LoadTaskState the call succeeds and the instruction does not happen. On a
// real machine `ltr` sets the TSS descriptor's busy bit, so the descriptor reads as type 11 afterwards; on a host that never
// ran it the same descriptor still reads as type 9. A unit that did not model this distinction tested the available-TSS
// path only, and the observation refused every real machine -- which is what the first guest run of this packet found.
//
// So the seam carries the hardware fact explicitly. A unit that wants the post-`ltr` world sets it and lays out a type-11
// descriptor; a unit that leaves it zero gets the pre-`ltr` world, which is what its type-9 fixture describes.
extern "C" uint32_t gtos_gdt_host_ltr_ran;

// The readable window and the three counters.
extern "C" uint32_t gtos_gdt_host_window_base;
extern "C" uint32_t gtos_gdt_host_window_bytes;
extern "C" uint32_t gtos_gdt_host_illegal_reads;
extern "C" uint32_t gtos_gdt_host_descriptor_reads;
extern "C" uint32_t gtos_gdt_host_tss_reads;

// The machine-level reads, as a caller supplies them.
inline void SetTaskRegister(uint32_t selector) { gtos_gdt_host_str_selector = selector; }
inline void SetGdtRegister(uint32_t base, uint32_t limit) {
    gtos_gdt_host_sgdt_base = base;
    gtos_gdt_host_sgdt_limit = limit;
}
// Declare whether the modelled machine executed `ltr`, which is what decides the descriptor's busy bit.
inline void SetTaskRegisterExecuted(bool executed) { gtos_gdt_host_ltr_ran = executed ? 1u : 0u; }

// The window the observation may read. A read outside it is charged to illegal reads and returns zero, which is the only
// thing a host can do in place of a fault.
inline void SetWindow(uint32_t base, uint32_t bytes) {
    gtos_gdt_host_window_base = base;
    gtos_gdt_host_window_bytes = bytes;
}

// Zero every counter. Each case starts from a known count, so a count observed afterwards belongs to that case alone and
// not to a case that ran before it.
inline void ResetCounters() {
    gtos_gdt_host_reads = 0;
    gtos_gdt_host_illegal_reads = 0;
    gtos_gdt_host_descriptor_reads = 0;
    gtos_gdt_host_tss_reads = 0;
}

}  // namespace testseam
}  // namespace gtos

#endif  // GTOS_TESTS_GDT_HOST_SEAM_H

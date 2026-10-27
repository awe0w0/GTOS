#ifndef GTOS_COMMON_BOOT_DIAGNOSTIC_H
#define GTOS_COMMON_BOOT_DIAGNOSTIC_H
#include <common/types.h>
namespace gtos { namespace common {
// Pure classification of the bounded boot command line for the private diagnostic stops. No heap, no device IO, no
// globals, no allocation: the helper reads only the caller's span and returns a small value.
//
// The only recognised options are the whole space-delimited tokens
//   bootdiag-stop=memory   halt after the real physical and heap self-tests succeed
//   bootdiag-stop=tables   halt after the GDT and IDT are constructed and loaded
//   bootdiag-stop=ap       halt after the AP handshake diagnostic, before any worker or service
//   bootdiag-stop=paging   halt after real bootstrap paging is enabled and the owned mapping probe passes
//   bootdiag-stop=workers  halt after the worker pool ran a real job set to completion and parked its APs
//   bootdiag-stop=runtime-init
//                          halt after the BSP prepared and activated the native runtime stacks, before any process
//   bootdiag-stop=runtime-objects
//                          halt after the BSP created, proved, exited and reaped fixed native objects
//   bootdiag-stop=runtime-recovery
//                          halt after the BSP drove the runtime into a real capacity refusal and recovered from it
// Anything else that carries the option prefix is a rejection, never a silent fall-through, because a malformed
// diagnostic must not let the machine continue into late startup.
//
// The legacy "bootlog-fail" token keeps its E99 precedence. The kernel checks that token first, so a command line
// carrying both never reaches this helper's stop arms.
//
// Span contract, stated exactly:
//
//   `span`     the first readable byte of the command line. A null span is "no command line" and yields StopNone.
//   `capacity` the number of readable bytes the caller guarantees at `span`. It is NOT an upper bound the helper may
//              silently reduce: capacity 0 yields StopNone, and capacity above MaximumCommandLine is a refusal,
//              because a caller that declares more than the kernel's own bound has handed over a line this helper is
//              not allowed to interpret. There is no implicit clamp.
//
// Within those readable bytes the scan stops at the first NUL, or at the end of the span if no NUL appears. A token
// that carries the option prefix and reaches the end of the span without a space or a NUL is unterminated and is
// refused: a truncated line must never look like a valid option. Every other token may run to the end harmlessly.
class BootDiagnostic {
  public:
    enum Stop {
        StopNone = 0,    // no diagnostic option: the normal boot flow is unchanged
        StopMemory = 1,  // halt after the real physical and heap self-tests succeed
        StopTables = 2,  // halt after the GDT and IDT are constructed and loaded
        StopReject = 3,  // malformed option: fail closed before any late startup
        StopAp = 4,      // halt after the AP handshake diagnostic
        StopPaging = 5,  // halt after real bootstrap paging is enabled
        StopWorkers = 6, // halt after the worker pool drained, closed admission and parked
    // Halt after the BSP prepared and activated the native runtime's kernel stacks. This is the first stop that reaches
    // the runtime's own initialisation, and it stops before any process exists: no Create, no ELF load, no user mode, no
    // interrupt, no scheduler entry and no service. It is single-CPU only by construction, because the runtime is a
    // BSP-only prototype whose stacks are not safe to hand to an AP that has not been told about them.
    StopRuntimeInit = 7,
    // Halt after the BSP created fixed native objects, proved their private memory, exited and reaped them. The objects
    // exist as data and are never dispatched: no user instruction runs, no task is scheduled, no interrupt is enabled.
    StopRuntimeObjects = 8,
    // Halt after the BSP filled the runtime's own slot table to its real limit, was refused by the runtime's own
    // capacity check rather than by an injected fault, and then recovered in the same instance: the refused attempt
    // left nothing behind, a reaped slot became usable again, and the next Create succeeded with a live object.
    StopRuntimeRecovery = 9,
    // Halt after the BSP produced a refusal that came from the frame allocator rather than from the slot table: a spare
    // slot existed the whole time, a live sibling object held its own memory, and a bounded set of frames the BSP had
    // claimed for itself made the next create fail partway through its own address-space construction. The state sets
    // either side of that call are compared element by element, not by totals, so a leak that moved one frame from free
    // to allocated while leaving the sum unchanged cannot pass. The pressure is then withdrawn, the same instance serves
    // a create, and every object is reaped back to the frame set the runtime started from.
    StopRuntimeFrameRecovery = 10,
    };
    enum Reject {
        RejectNone = 0,
        RejectEmptyValue = 1,
        RejectUnknownValue = 2,
        RejectDuplicate = 3,
        RejectUnterminated = 4,
        RejectOversizeSpan = 5,
    };
    // Phase ordering. A stop may only fire once its phase is reached; the kernel calls the stop arm at the matching
    // cut. The order is the real boot order, so a later phase strictly dominates an earlier one.
    enum Phase {
        PhaseCommandLine = 0,
        PhaseMemory = 1,
        PhaseTables = 2,
        PhaseAp = 3,
        PhasePaging = 4,
        PhaseWorkers = 5,
  // The runtime initialisation belongs to its own phase, after the worker phase, because it needs a sealed directory and a
  // CPU set that will not grow.
  PhaseRuntimeInit = 6,
  // The object lifecycle belongs to its own phase, after the runtime itself is initialised.
  PhaseRuntimeObjects = 7,
  // The recovery probe needs an initialised runtime with a live object table, so it comes after the lifecycle phase.
  PhaseRuntimeRecovery = 8,
  // The frame-pressure probe needs the same initialised runtime, and it comes after the capacity probe because it
  // measures the other refusal the runtime can produce: not "no slot" but "no frames".
  PhaseRuntimeFrameRecovery = 9,
    };
    struct Decision {
        uint32_t stop;
        uint32_t reject;
    };
    enum {
        // The kernel's existing option scanner is bounded at 256 bytes and the Multiboot command line has no length
        // field, so the same bound applies here.
        MaximumCommandLine = 256,
        // "bootdiag-stop=" including the trailing '='.
        OptionPrefixLength = 14,
    };
    static Decision Parse(const char* span, uint32_t capacity);
    // The single line each stop prints. Null for StopNone.
    static const char* Marker(uint32_t stop);
    // A stable name for a rejection, so a guest log records why it refused.
    static const char* RejectName(uint32_t reject);
    // The phase a stop belongs to. StopReject belongs to the command-line phase.
    static uint32_t RequiredPhase(uint32_t stop);
    // The option value a stop is selected by, so a caller can name the stop it accepted without duplicating a table.
    static const char* ValueName(uint32_t stop);
};
} }
#endif

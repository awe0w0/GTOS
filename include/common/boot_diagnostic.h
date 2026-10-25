#ifndef GTOS_COMMON_BOOT_DIAGNOSTIC_H
#define GTOS_COMMON_BOOT_DIAGNOSTIC_H
#include <common/types.h>
namespace gtos { namespace common {
// Pure classification of the bounded boot command line for the private diagnostic stops. No heap, no device IO, no
// globals, no allocation: the helper reads only the caller's span and returns a small value.
//
// The only recognised option is the whole space-delimited token "bootdiag-stop=memory" or "bootdiag-stop=tables".
// Anything else that carries the option prefix is a rejection, never a silent fall-through, because a malformed
// diagnostic must not let the machine continue into late startup.
//
// The legacy "bootlog-fail" token keeps its E99 precedence. The kernel checks that token first, so a command line
// carrying both never reaches this helper's stop arms.
//
// Span contract, stated exactly because the earlier packet left it ambiguous:
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
        StopNone = 0,   // no diagnostic option: the normal boot flow is unchanged
        StopMemory = 1, // halt after the real physical and heap self-tests succeed
        StopTables = 2, // halt after the GDT and IDT are constructed and loaded
        StopReject = 3, // malformed option: fail closed before any late startup
    };
    enum Reject {
        RejectNone = 0,
        RejectEmptyValue = 1,
        RejectUnknownValue = 2,
        RejectDuplicate = 3,
        RejectUnterminated = 4,
        RejectOversizeSpan = 5,
    };
    // Phase ordering. A stop may only fire once its phase is reached; the kernel calls the stop arm at the matching cut.
    enum Phase { PhaseCommandLine = 0, PhaseMemory = 1, PhaseTables = 2 };
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
};
} }
#endif

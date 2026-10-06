#ifndef __GTOS__PROCESS__FAULT_POLICY_H
#define __GTOS__PROCESS__FAULT_POLICY_H
#include <multitasking.h>
namespace gtos { namespace process {
    // Pure classification shared by the live handler and source-level tests.
    // Caller must ALSO verify current native task identity and actual CR3.
    inline bool RecoverableUserFault(const CPUState& cpu) {
        if ((cpu.cs & 3) != 3) return false;
        switch (cpu.vector) {
        case 0: case 1: case 3: case 4: case 5: case 6: case 7:
        case 10: case 11: case 12: case 13: case 16: case 17: case 19:
            return true;
        case 14:
            // RSVD or an implicit supervisor-access PF indicates broken kernel
            // page tables/descriptor access, even with saved user CS.
            return (cpu.error & 4) && !(cpu.error & 8);
        default:
            // NMI, double fault, machine check and unknown/reserved vectors.
            return false;
        }
    }
} }
#endif

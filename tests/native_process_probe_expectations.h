#ifndef GTOS_NATIVE_PROCESS_PROBE_EXPECTATIONS_H
#define GTOS_NATIVE_PROCESS_PROBE_EXPECTATIONS_H
#include <process/native_runtime.h>
namespace native_process_tests {
    // QEMU 8.2's MOV-SS block ending can report a user single-step #DB where
    // newer QEMU suppresses it. This exception is accepted ONLY for fixture18,
    // before its second syscall, with verified user/CR3/TF provenance. It is
    // not permission for any other fault or a kernel-origin debug exception.
    inline bool LegacyMovSsDebug(uint32_t mode, const gtos::process::NativeStatus& state,
                                 uint32_t kernelDirectory) {
        return mode == 18 && state.id && !state.live && !state.reaped
            && state.exitCode == 0x80000001U && state.faultVector == 1
            && state.faultError == 0 && state.faultAddress == 0
            && state.observedCs == 0x23 && state.directory
            && state.observedCr3 == state.directory && state.directory != kernelDirectory
            && (state.observedEflags & 0x3302U) == 0x302U
            && state.systemCalls == 1;
    }
    // QEMU 4.2 tests CR0.TS before the missing-OSFXSR #UD condition for PXOR.
    // Accept its contained #NM only for the OSFXSR-off SSE fixture, never as
    // a general substitute for another expected exception.
    inline bool LegacySsePriority(uint32_t mode, bool osfxsr,
                                  const gtos::process::NativeStatus& state,
                                  uint32_t kernelDirectory) {
        return mode == 12 && !osfxsr && state.id && !state.live && !state.reaped
            && state.exitCode == 0x80000007U && state.faultVector == 7
            && state.faultError == 0 && state.faultAddress == 0
            && state.observedCs == 0x23 && state.directory
            && state.observedCr3 == state.directory && state.directory != kernelDirectory
            && (state.observedEflags & 0x3302U) == 0x202U
            && state.systemCalls == 1;
    }
    // Older QEMU checks disabled EFER.SCE before SYSRET's CPL0 requirement.
    // Keep the alternate tied to this one instruction and verified disabled
    // fast entry, rather than accepting #UD for arbitrary protection probes.
    inline bool LegacySysretPriority(uint32_t mode, bool haveSyscall, bool sceDisabled,
                                     const gtos::process::NativeStatus& state,
                                     uint32_t kernelDirectory) {
        return mode == 27 && haveSyscall && sceDisabled && state.id
            && !state.live && !state.reaped
            && state.exitCode == 0x80000006U && state.faultVector == 6
            && state.faultError == 0 && state.faultAddress == 0
            && state.observedCs == 0x23 && state.directory
            && state.observedCr3 == state.directory && state.directory != kernelDirectory
            && (state.observedEflags & 0x3302U) == 0x202U
            && state.systemCalls == 1;
    }
}
#endif

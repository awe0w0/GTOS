// Diagnostic-only qualifier. Never linked into the ordinary or strict kernel.
// Called under InterruptGuard after successful SSE2 activation and before any
// native admission, while the native ownership state is KernelNeutral.
#include <process/native_fp.h>
using namespace gtos::process;
struct PointerProbe {
    NativeFpContext original, fldSource, fldObserved, fxSource, fxObserved;
    uint32_t flagsBefore, flagsAfter, cr0Before, cr0After, cr4Before, cr4After;
} __attribute__((aligned(16)));
static_assert(__builtin_offsetof(PointerProbe, flagsBefore) == 2720, "probe assembly offset");
static PointerProbe probe;
extern "C" uint32_t native_fp_desktop_pointer_probe_asm(PointerProbe*);
static bool Same(const uint8_t* a, const uint8_t* b, uint32_t offset, uint32_t bytes) {
    for (uint32_t i = offset; i < offset + bytes; ++i) if (a[i] != b[i]) return false;
    return true;
}
static bool ZeroPointers(const uint8_t* env) {
    return NativeFpGet32(env + 12) == 0 && NativeFpGet16(env + 16) == 0
        && NativeFpGet32(env + 20) == 0 && NativeFpGet16(env + 24) == 0;
}
static bool Neutral(const NativeFpContext& image) {
    if (NativeFpGet16(image.fx) != 0x37F || NativeFpGet16(image.fx + 2)
        || image.fx[4] || NativeFpGet16(image.fx + 6)
        || NativeFpGet32(image.fx + 24) != 0x1F80
        || NativeFpGet16(image.env) != 0x37F || NativeFpGet16(image.env + 4)
        || NativeFpGet16(image.env + 8) != 0xFFFF
        || (NativeFpGet16(image.env + 18) & 0x7FF) || !ZeroPointers(image.env)) return false;
    for (uint32_t reg = 0; reg < 8; ++reg)
        for (uint32_t b = 0; b < 10; ++b) if (image.fx[32 + reg * 16 + b]) return false;
    for (uint32_t b = 160; b < 288; ++b) if (image.fx[b]) return false;
    return true;
}
extern "C" bool native_fp_desktop_qualify_pointer_gap() {
    NativeFpScrub(&probe, sizeof(probe));
    NativeFpPut16(probe.fldSource.env, 0x067F);
    NativeFpPut16(probe.fldSource.env + 4, 0x0800);
    NativeFpPut16(probe.fldSource.env + 8, 0xFFFF);
    NativeFpPut32(probe.fldSource.env + 12, 0x12345678);
    NativeFpPut16(probe.fldSource.env + 16, 0x23);
    NativeFpPut32(probe.fldSource.env + 20, 0x34567890);
    NativeFpPut16(probe.fldSource.env + 24, 0x2B);
    NativeFpPut16(probe.fxSource.fx, 0x0B7F);
    NativeFpPut16(probe.fxSource.fx + 2, 0x1000);
    NativeFpPut32(probe.fxSource.fx + 8, 0x23456789);
    NativeFpPut16(probe.fxSource.fx + 12, 0x2B);
    NativeFpPut32(probe.fxSource.fx + 16, 0x456789AB);
    NativeFpPut16(probe.fxSource.fx + 20, 0x23);
    NativeFpPut32(probe.fxSource.fx + 24, 0x5FA1);
    for (uint32_t i = 0; i < 128; ++i) probe.fxSource.fx[160 + i] = (uint8_t)(0x91 + i * 7);
    bool valid = native_fp_desktop_pointer_probe_asm(&probe) == 1;
    valid = valid && !(probe.flagsBefore & 0x200) && !(probe.flagsAfter & 0x200)
        && probe.cr0Before == probe.cr0After && probe.cr4Before == probe.cr4After
        && Neutral(probe.original);
    // Require the specific known all-zero omission, not arbitrary corruption.
    valid = valid && ZeroPointers(probe.fldObserved.env) && ZeroPointers(probe.fxObserved.env)
        && NativeFpGet16(probe.fldObserved.env) == 0x067F
        && NativeFpGet16(probe.fldObserved.env + 4) == 0x0800
        && NativeFpGet16(probe.fldObserved.env + 8) == 0xFFFF
        && !(NativeFpGet16(probe.fldObserved.env + 18) & 0x7FF)
        && NativeFpGet16(probe.fxObserved.env) == 0x0B7F
        && NativeFpGet16(probe.fxObserved.env + 4) == 0x1000
        && NativeFpGet16(probe.fxObserved.env + 8) == 0xFFFF
        && !(NativeFpGet16(probe.fxObserved.env + 18) & 0x7FF)
        && Same(probe.fxSource.fx, probe.fxObserved.fx, 0, 5)
        && Same(probe.fxSource.fx, probe.fxObserved.fx, 6, 2)
        && Same(probe.fxSource.fx, probe.fxObserved.fx, 24, 4)
        && Same(probe.fxSource.fx, probe.fxObserved.fx, 160, 128);
    for (uint32_t reg = 0; reg < 8; ++reg)
        valid = valid && Same(probe.fxSource.fx, probe.fxObserved.fx, 32 + reg * 16, 10);
    NativeFpScrub(&probe, sizeof(probe));
    return valid;
}

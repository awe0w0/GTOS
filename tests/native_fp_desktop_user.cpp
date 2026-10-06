// Two independent ELF binaries sustain different FP state during desktop/AP work.
#include "../apps/native/probe.h"
#ifndef GTOS_FP_DESKTOP_FAULT
#define GTOS_FP_DESKTOP_FAULT 0
#endif
#ifndef GTOS_FP_DESKTOP_POINTER_DIAGNOSTIC
#define GTOS_FP_DESKTOP_POINTER_DIAGNOSTIC 0
#endif
volatile ProbeData nativeData = {GTOS_FP_DESKTOP_FAULT ? 0xA11CE001U : 0xB22CE002U, 0, 0};
struct Image { uint8_t fx[512], env[28], pad[4]; } __attribute__((aligned(16)));
static Image expected, observed;
static uint32_t xmm[32] __attribute__((aligned(16)));
static int32_t x87[8];
extern "C" void native_fp_desktop_seed(const uint32_t*, const int32_t*, const uint16_t*, const uint32_t*);
extern "C" void native_fp_desktop_capture(Image*);
static bool Equal(uint32_t offset, uint32_t bytes, bool env = false) {
    const uint8_t* a = env ? expected.env : expected.fx;
    const uint8_t* b = env ? observed.env : observed.fx;
    for (uint32_t i = offset; i < offset + bytes; ++i) if (a[i] != b[i]) return false;
    return true;
}
static void Check() {
    native_fp_desktop_capture(&observed);
    bool equal = Equal(0, 5) && Equal(24, 4) && Equal(160, 128)
        && Equal(0, 2, true) && Equal(4, 2, true) && Equal(8, 2, true)
        && Equal(6, 2) && Equal(18, 2, true);
    // Only this separately compiled diagnostic skips the independently qualified
    // FIP/FCS/FDP/FDS omission. Opcode, payloads and controls stay strict.
    if (!GTOS_FP_DESKTOP_POINTER_DIAGNOSTIC)
        equal = equal && Equal(12, 4, true) && Equal(16, 2, true) && Equal(20, 6, true);
    for (uint32_t i = 0; i < 8; ++i) equal = equal && Equal(32 + i * 16, 10);
    if (!equal) { ++nativeData.errors; Exit(0xF9); }
}
extern "C" __attribute__((section(".text.body"), noreturn)) void NativeEntry() {
    const uint32_t seed = GTOS_FP_DESKTOP_FAULT ? 0x19283746U : 0xAABB1234U;
    for (uint32_t i = 0; i < 32; ++i) xmm[i] = seed + i * 0x01010101U;
    for (uint32_t i = 0; i < 8; ++i) x87[i] = (GTOS_FP_DESKTOP_FAULT ? 1234 : 5678) + i;
    const uint16_t control = GTOS_FP_DESKTOP_FAULT ? 0x0B7F : 0x067F;
    const uint32_t mxcsr = GTOS_FP_DESKTOP_FAULT ? 0x5FA1 : 0x3F84;
    native_fp_desktop_seed(xmm, x87, &control, &mxcsr);
    native_fp_desktop_capture(&expected);
    if (Call(GTOS_SYS_ABI) != GTOS_NATIVE_ABI_VERSION) Exit(0xF1);
    Check();
#if GTOS_FP_DESKTOP_FAULT
    const char live[] = "NATIVE FP CPL3 FAULT LIVE\n";
#else
    const char live[] = "NATIVE FP CPL3 PEER LIVE\n";
#endif
    Call(GTOS_SYS_WRITE, (uint32_t)live, sizeof(live) - 1);
    Check();
    const uint32_t start = Call(GTOS_SYS_TICKS);
    while (Call(GTOS_SYS_TICKS) - start < (GTOS_FP_DESKTOP_FAULT ? 400U : 700U)) {
        Check();
        // Long enough for real timer and input IRQ entry with seeded FP live.
        for (volatile uint32_t n = 0; n < 100000; ++n) ++nativeData.progress;
        Check();
        if ((nativeData.progress & 0xFFF) == 0) Call(GTOS_SYS_YIELD);
    }
    if (GTOS_FP_DESKTOP_FAULT) {
        const char text[] = "NATIVE FP CPL3 FAULT STATE VERIFIED\n";
        Call(GTOS_SYS_WRITE, (uint32_t)text, sizeof(text) - 1);
        Check();
        *(volatile uint32_t*)0x100000 = 0;
        Exit(0xFA);
    }
    const char text[] = "NATIVE CPL3 PEER SURVIVED\n";
    if (Call(GTOS_SYS_WRITE, (uint32_t)text, sizeof(text) - 1) != sizeof(text) - 1) Exit(0xF3);
    Check(); Exit(0);
}

// Deliberately untrusted CPL3 test program. Never call this in kernel mode.
#include "probe.h"
volatile ProbeData nativeData = {0xA11CE001U, 0, 0};
extern "C" __attribute__((section(".text.body"), noreturn)) void NativeEntry() {
    if (Call(GTOS_SYS_ABI) != GTOS_NATIVE_ABI_VERSION)
        Exit(0xE1);
    if (nativeData.signature != 0xA11CE001U)
        Exit(0xE2);
    uint32_t start = Call(GTOS_SYS_TICKS);
    while (Call(GTOS_SYS_TICKS) - start < 20)
        for (uint32_t work = 0; work < 128; ++work)
            ++nativeData.progress;
    if ((int32_t)Call(GTOS_SYS_WRITE, 0x100000, 16) != GTOS_ERR_BAD_ADDRESS)
        ++nativeData.errors;
    // Last two bytes of the code page followed by an unmapped page. The
    // checked syscall must reject the whole range before printing any byte.
    if ((int32_t)Call(GTOS_SYS_WRITE, 0x40000FFEU, 4) != GTOS_ERR_BAD_ADDRESS)
        ++nativeData.errors;
    if (nativeData.errors)
        Exit(0xE3);
    const char text[] = "NATIVE CPL3 FAULT PROBE\n";
    if (Call(GTOS_SYS_WRITE, (uint32_t)text, sizeof(text) - 1) != sizeof(text) - 1)
        Exit(0xE4);
    *(volatile uint32_t *)0x100000 = 0xBADCAFE;
    Exit(0xE5); // Reaching this is an isolation failure.
}

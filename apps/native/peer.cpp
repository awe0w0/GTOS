// Independent CPL3 peer with identical virtual addresses but private backing.
#include "probe.h"
#ifndef GTOS_NATIVE_PEER_TICKS
#define GTOS_NATIVE_PEER_TICKS 100
#endif
volatile ProbeData nativeData = {0xB22CE002U, 0, 0};
extern "C" __attribute__((section(".text.body"), noreturn)) void NativeEntry() {
    if (Call(GTOS_SYS_ABI) != GTOS_NATIVE_ABI_VERSION)
        Exit(0xF1);
    uint32_t start = Call(GTOS_SYS_TICKS);
    while (Call(GTOS_SYS_TICKS) - start < GTOS_NATIVE_PEER_TICKS) {
        if (nativeData.signature != 0xB22CE002U)
            Exit(0xF2);
        for (uint32_t work = 0; work < 1024; ++work)
            ++nativeData.progress;
    }
    const char text[] = "NATIVE CPL3 PEER SURVIVED\n";
    if (Call(GTOS_SYS_WRITE, (uint32_t)text, sizeof(text) - 1) != sizeof(text) - 1)
        Exit(0xF3);
    Exit(0);
}

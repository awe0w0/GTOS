// Diagnostic variant of apps/native/fault.cpp, used only by the reclaim ISO.
// The default payload is unchanged and stays the shipped module; this one keeps
// the same signature, progress and exact user page fault, and additionally owns
// a live published window with an unfinished draft when it faults, so StopGroup
// has a real object to reclaim.
#include "reclaim.h"

#ifndef GTOS_RECLAIM_FAULT_TICKS
// Long enough for the desktop to open and paint the window, short enough that the
// peer is still alive when this process faults (that is the peer-after-fault
// contract the demo already enforces) and well inside the 1000-tick demo budget.
#define GTOS_RECLAIM_FAULT_TICKS 300
#endif

volatile ReclaimData nativeData = {0xA11CE001U, 0, 0, 0, 0};
static unsigned char reclaimFrame[GTOS_RECLAIM_BYTES];
static unsigned char reclaimChunk[GTOS_WINDOW_WRITE_LIMIT];
static GtosWindowInfoResponse reclaimInfo;

extern "C" __attribute__((section(".text.body"), noreturn)) void NativeEntry() {
    if (Call(GTOS_SYS_ABI) != GTOS_NATIVE_ABI_VERSION)
        Exit(0xE1);
    if (nativeData.signature != 0xA11CE001U)
        Exit(0xE2);
    // The window comes first: it must already exist when the progress loop runs,
    // so the kernel can observe a live owner with a draft before the fault.
    const uint32_t handle = ReclaimOpenWindow(reclaimFrame, reclaimChunk, 255);
    if (!handle)
        Exit(0xE7);
    nativeData.windowHandle = handle;
    nativeData.windowMagic = ReclaimVerify(handle, false, &reclaimInfo);
    if (!nativeData.windowMagic)
        Exit(0xE8);
    const char ready[] = "NATIVE RECLAIM FAULT WINDOW LIVE\n";    if (Call(GTOS_SYS_WRITE, (uint32_t)ready, sizeof(ready) - 1) != sizeof(ready) - 1)
        Exit(0xE9);
    uint32_t start = Call(GTOS_SYS_TICKS);
    while (Call(GTOS_SYS_TICKS) - start < GTOS_RECLAIM_FAULT_TICKS)
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

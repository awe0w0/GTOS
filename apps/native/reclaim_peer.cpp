// Diagnostic variant of apps/native/peer.cpp, used only by the reclaim ISO.
// It keeps the peer role contract (its own signature, live progress and the
// "NATIVE CPL3 PEER SURVIVED" exit) and additionally owns a live published
// window with an unfinished draft and a pending input record when it exits
// normally, so StopGroup has a real object to reclaim on the exit path too.
//
// The window is created only after the fault process has already been stopped,
// so this window takes the slot the fault process used and the desktop layout is
// identical to the single-window case.
#include "reclaim.h"

#ifndef GTOS_RECLAIM_PEER_START
// The fault process faults at 300 ticks and its window is reclaimed at that Stop,
// so this window is created afterwards and cannot be covered by it.
#define GTOS_RECLAIM_PEER_START 400
#endif
#ifndef GTOS_RECLAIM_PEER_TICKS
// The whole demo budget is 1000 ticks. The peer must still be alive when the
// fault process dies, and must exit on its own well before that budget ends.
#define GTOS_RECLAIM_PEER_TICKS 820
#endif

volatile ReclaimData nativeData = {0xB22CE002U, 0, 0, 0, 0};
static unsigned char reclaimFrame[GTOS_RECLAIM_BYTES];
static unsigned char reclaimChunk[GTOS_WINDOW_WRITE_LIMIT];
static GtosWindowInfoResponse reclaimInfo;

extern "C" __attribute__((section(".text.body"), noreturn)) void NativeEntry() {
    if (Call(GTOS_SYS_ABI) != GTOS_NATIVE_ABI_VERSION)
        Exit(0xF1);
    uint32_t start = Call(GTOS_SYS_TICKS);
    // Keep the original peer progress contract while the fault process runs and
    // its window is stopped.
    while (Call(GTOS_SYS_TICKS) - start < GTOS_RECLAIM_PEER_START) {
        if (nativeData.signature != 0xB22CE002U)
            Exit(0xF2);
        for (uint32_t work = 0; work < 1024; ++work)
            ++nativeData.progress;
    }
    const uint32_t handle = ReclaimOpenWindow(reclaimFrame, reclaimChunk, 96);
    if (!handle)
        Exit(0xF4);
    nativeData.windowHandle = handle;
    nativeData.windowMagic = ReclaimVerify(handle, false, &reclaimInfo);
    if (!nativeData.windowMagic)
        Exit(0xF6);
    const char ready[] = "NATIVE RECLAIM PEER WINDOW LIVE\n";
    if (Call(GTOS_SYS_WRITE, (uint32_t)ready, sizeof(ready) - 1) != sizeof(ready) - 1)
        Exit(0xF5);
    // Wait, inside the fixed budget, for one real input record routed by the
    // desktop. Nothing here writes into the queue: it only observes the ABI, and
    // the upgrade to the queue magic is the evidence that the record arrived.
    for (;;) {
        if (nativeData.signature != 0xB22CE002U)
            Exit(0xF2);
        for (uint32_t work = 0; work < 1024; ++work)
            ++nativeData.progress;
        GtosWindowInfoRequest request;
        request.version = GTOS_WINDOW_ABI_VERSION;
        request.handle = handle;
        request.result_va = (uint32_t)&reclaimInfo;
        if ((int32_t)Call(GTOS_SYS_WINDOW_INFO, (uint32_t)&request, sizeof(request)) == 0 &&
            reclaimInfo.queue_depth && reclaimInfo.lifecycle == GTOS_WINDOW_LIVE)
            nativeData.windowMagic = GTOS_RECLAIM_QUEUE_MAGIC;
        if (Call(GTOS_SYS_TICKS) - start >= GTOS_RECLAIM_PEER_TICKS)
            break;
    }
    // A run that never received input must not exit as if it had.
    if (nativeData.windowMagic != GTOS_RECLAIM_QUEUE_MAGIC)
        Exit(0xF7);
    const char text[] = "NATIVE CPL3 PEER SURVIVED\n";
    if (Call(GTOS_SYS_WRITE, (uint32_t)text, sizeof(text) - 1) != sizeof(text) - 1)
        Exit(0xF3);
    Exit(0);
}

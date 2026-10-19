#ifndef GTOS_NATIVE_RECLAIM_H
#define GTOS_NATIVE_RECLAIM_H
// Shared by the two reclaim diagnostic variants. It is a separate header so the
// default fault/peer payloads keep their exact three-field contract: only these
// diagnostic builds append the two proof words the kernel reads back.
#include "probe.h"
#include <process/window_abi.h>
#include <process/native_reclaim_abi.h>

// The diagnostic window is 16x16, so one whole frame is 1024 bytes. The native
// data page is a single 4 KiB page, and the frame plus the submit chunk plus the
// proof words still fit inside it.
#define GTOS_RECLAIM_PIXELS  16U
#define GTOS_RECLAIM_STRIDE  64U
#define GTOS_RECLAIM_BYTES   (GTOS_RECLAIM_PIXELS * GTOS_RECLAIM_STRIDE)

// The kernel reads the first three words with its unchanged NativeProbeData and
// the last two with its reclaim proof struct, so the original signature/progress/
// errors contract is preserved bit for bit.
struct ReclaimData {
    uint32_t signature, progress, errors;
    uint32_t windowHandle, windowMagic;
};

// Written only after the window has been verified live, published and holding an
// unfinished draft. A zero here means the window work never completed, and the
// kernel treats that as a failure rather than skipping the check. The two proof
// words themselves live in <process/native_reclaim_abi.h> so the kernel and these
// payloads cannot drift apart.

// A 16x16 premultiplied frame with one mid alpha, so the desktop composites it
// over the checkerboard and the client rectangle stays measurable. Every pixel
// keeps red >= green >= blue so the whole chunk is a legal premultiplied pixel.
static inline void ReclaimFillFrame(unsigned char *frame, uint32_t tone) {
    const unsigned alpha = 128;
    for (unsigned i = 0; i < GTOS_RECLAIM_BYTES; i += 4) {
        const unsigned base = 0x30U + (i / 4) % 4;
        frame[i + 0] = (unsigned char)(base * tone / 255U * alpha / 255U);
        frame[i + 1] = (unsigned char)(base * tone / 255U * alpha / 255U);
        frame[i + 2] = (unsigned char)(base * tone / 255U * alpha / 255U);
        frame[i + 3] = (unsigned char)alpha;
    }
}

// Creates the window, publishes one whole frame and then leaves a half-written
// second draft. Returns the handle, or 0 when any step failed.
static inline uint32_t ReclaimOpenWindow(unsigned char *frame, unsigned char *chunk,
                                         uint32_t tone) {
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = GTOS_RECLAIM_PIXELS;
    create.height = GTOS_RECLAIM_PIXELS;
    create.stride = GTOS_RECLAIM_STRIDE;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int32_t handle = (int32_t)Call(GTOS_SYS_WINDOW_CREATE, (uint32_t)&create,
                                         sizeof(create));
    if (handle <= 0) return 0;
    ReclaimFillFrame(frame, tone);
    GtosWindowSubmitRequest submit;
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = (uint32_t)handle;
    submit.source = (uint32_t)frame;
    for (unsigned offset = 0; offset < GTOS_RECLAIM_BYTES;
         offset += GTOS_WINDOW_WRITE_LIMIT) {
        submit.offset = offset;
        submit.length = GTOS_WINDOW_WRITE_LIMIT;
        if ((int32_t)Call(GTOS_SYS_WINDOW_SUBMIT, (uint32_t)&submit, sizeof(submit))
            != (int32_t)GTOS_WINDOW_WRITE_LIMIT)
            return 0;
    }
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (uint32_t)handle;
    if ((int32_t)Call(GTOS_SYS_WINDOW_PRESENT, (uint32_t)&control, sizeof(control)) != 0)
        return 0;
    // A second frame, stopped halfway: progress is non-zero and below the total,
    // so the bank really holds an unfinished draft when the owner stops.
    for (unsigned i = 0; i < GTOS_WINDOW_WRITE_LIMIT; ++i) chunk[i] = (unsigned char)i;
    submit.offset = 0;
    submit.source = (uint32_t)chunk;
    submit.length = GTOS_WINDOW_WRITE_LIMIT;
    if ((int32_t)Call(GTOS_SYS_WINDOW_SUBMIT, (uint32_t)&submit, sizeof(submit))
        != (int32_t)GTOS_WINDOW_WRITE_LIMIT)
        return 0;
    return (uint32_t)handle;
}

// Reads the window back through the ABI. `queueWanted` makes a pending input
// record part of the contract instead of an optional extra.
static inline uint32_t ReclaimVerify(uint32_t handle, bool queueWanted,
                                     GtosWindowInfoResponse *info) {
    GtosWindowInfoRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = handle;
    request.result_va = (uint32_t)info;
    if ((int32_t)Call(GTOS_SYS_WINDOW_INFO, (uint32_t)&request, sizeof(request)) != 0)
        return 0;
    if (info->handle != handle || info->lifecycle != GTOS_WINDOW_LIVE) return 0;
    if (!info->frame_generation) return 0;
    if (!info->progress || info->progress >= info->total) return 0;
    if (queueWanted) {
        if (!info->queue_depth) return 0;
        return GTOS_RECLAIM_QUEUE_MAGIC;
    }
    return GTOS_RECLAIM_MAGIC;
}
#endif

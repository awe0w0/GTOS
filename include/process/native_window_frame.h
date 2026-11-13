#ifndef GTOS_PROCESS_NATIVE_WINDOW_FRAME_H
#define GTOS_PROCESS_NATIVE_WINDOW_FRAME_H

#include <process/window_client.h>

namespace gtos {
namespace process {

// Publish one complete, tightly packed premultiplied RGBA8 frame.
// The caller serializes frame writers for the handle. A failed chunk is
// returned to the caller, and an incomplete draft is never presented.
inline int PresentNativeWindowFrame(unsigned handle, const void* pixels,
                                    unsigned bytes) {
    if (!pixels) return GTOS_WINDOW_ERR_BAD_ADDRESS;
    GtosWindowInfoResponse info;
    const GtosWindowInfoRequest query{
        GTOS_WINDOW_ABI_VERSION, handle, (unsigned)&info};
    const int queried = WindowInfo(&query);
    if (queried < 0) return queried;
    if (bytes != info.total) return GTOS_WINDOW_ERR_BAD_SIZE;
    if (info.lifecycle != GTOS_WINDOW_LIVE || info.progress != 0)
        return GTOS_WINDOW_ERR_BAD_STATE;

    const auto* data = static_cast<const unsigned char*>(pixels);
    for (unsigned offset = 0; offset < bytes;) {
        const unsigned remaining = bytes - offset;
        const unsigned chunk = remaining < GTOS_WINDOW_WRITE_LIMIT
            ? remaining : GTOS_WINDOW_WRITE_LIMIT;
        const GtosWindowSubmitRequest request{
            GTOS_WINDOW_ABI_VERSION, handle, offset, (unsigned)(data + offset), chunk};
        const int written = WindowSubmit(&request);
        if (written < 0) return written;
        if ((unsigned)written != chunk) return GTOS_WINDOW_ERR_BAD_STATE;
        offset += chunk;
    }
    const GtosWindowControlRequest request{GTOS_WINDOW_ABI_VERSION, handle};
    return WindowPresent(&request);
}

inline int PresentLargeNativeWindowFrame(unsigned handle, const void* pixels,
                                         unsigned bytes) {
    const GtosWindowFrameRequest request{
        GTOS_WINDOW_ABI_VERSION, handle, (unsigned)pixels, bytes};
    return WindowCall(GTOS_SYS_WINDOW_FRAME, (unsigned)&request, sizeof(request));
}

} // namespace process
} // namespace gtos

#endif

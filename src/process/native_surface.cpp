#include <process/native_surface.h>
#ifndef GTOS_SURFACE_HOST_TEST
#include <memory/criticalsection.h>
#endif

using namespace gtos;
using namespace gtos::process;
namespace {
#ifndef GTOS_SURFACE_HOST_TEST
    typedef memory::InterruptGuard SurfaceGuard;
#else
    // Host invariants cover pure state; real checked-copy/concurrency/display
    // acceptance is exclusively the new actual QEMU consumer qualification.
    struct SurfaceGuard { SurfaceGuard() {} };
#endif
    NativeSurfaceBank bank;
}

NativeSurfaceBank::NativeSurfaceBank() : available(false), drafting(false),
    owner(0), handle(0), width(0), height(0), total(0), progress(0), nextHandle(1) {
    ScrubDraft();
    front.generation = front.width = front.height = 0;
    for (unsigned i = 0; i < GTOS_SURFACE_MAX_BYTES; ++i) front.rgba[i] = 0;
}
NativeSurfaceBank& NativeSurfaceBank::Instance() { return bank; }
void NativeSurfaceBank::SetAvailable(bool value) {
    SurfaceGuard guard;
    available = value;
}
void NativeSurfaceBank::ScrubDraft() {
    for (unsigned i = 0; i < GTOS_SURFACE_MAX_BYTES; ++i) draft[i] = 0;
    drafting = false;
    owner = handle = width = height = total = progress = 0;
}
int NativeSurfaceBank::Begin(unsigned ownerId, const GtosSurfaceBeginRequest& request, unsigned wireBytes) {
    SurfaceGuard guard;
    if (wireBytes != GTOS_SURFACE_BEGIN_REQUEST_BYTES) return GTOS_SURFACE_ERR_BAD_SIZE;
    if (request.version != GTOS_SURFACE_ABI_VERSION) return GTOS_SURFACE_ERR_UNSUPPORTED_VERSION;
    if (!ownerId) return GTOS_SURFACE_ERR_BAD_STATE;
    if (!available) return GTOS_SURFACE_ERR_UNAVAILABLE;
    if (request.format != GTOS_SURFACE_FORMAT_RGBA8_PREMULTIPLIED) return GTOS_SURFACE_ERR_UNSUPPORTED_FORMAT;
    if (!request.width || !request.height) return GTOS_SURFACE_ERR_RANGE;
    if (request.width > GTOS_SURFACE_MAX_WIDTH || request.height > GTOS_SURFACE_MAX_HEIGHT)
        return GTOS_SURFACE_ERR_TOO_LARGE;
    const unsigned row = request.width * 4;
    if (request.stride != row) return GTOS_SURFACE_ERR_BAD_SIZE;
    if (drafting) return GTOS_SURFACE_ERR_BUSY;
    // Never wrap or reuse a positive signed-i386 handle, including aborted
    // transactions. The same unique handle becomes the published generation.
    if (nextHandle > 0x7fffffffU) return GTOS_SURFACE_ERR_RANGE;
    ScrubDraft();
    owner = ownerId; handle = nextHandle++;
    width = request.width; height = request.height; total = row * height;
    drafting = true;
    return (int)handle;
}
int NativeSurfaceBank::ValidateWriteLocked(unsigned ownerId, const GtosSurfaceWriteRequest& request,
                                            unsigned wireBytes) const {
    if (wireBytes != GTOS_SURFACE_WRITE_REQUEST_BYTES) return GTOS_SURFACE_ERR_BAD_SIZE;
    if (request.version != GTOS_SURFACE_ABI_VERSION) return GTOS_SURFACE_ERR_UNSUPPORTED_VERSION;
    if (!ownerId || !drafting || owner != ownerId || !request.handle || handle != request.handle)
        return GTOS_SURFACE_ERR_BAD_STATE;
    if (request.length > GTOS_SURFACE_WRITE_LIMIT) return GTOS_SURFACE_ERR_TOO_LARGE;
    if (!request.length || (request.length & 3U)) return GTOS_SURFACE_ERR_BAD_SIZE;
    if (request.offset != progress || request.offset > total || request.length > total - request.offset)
        return GTOS_SURFACE_ERR_RANGE;
    return 0;
}
int NativeSurfaceBank::ValidateWrite(unsigned ownerId, const GtosSurfaceWriteRequest& request,
                                     unsigned wireBytes) const {
    SurfaceGuard guard;
    return ValidateWriteLocked(ownerId, request, wireBytes);
}
int NativeSurfaceBank::Write(unsigned ownerId, const GtosSurfaceWriteRequest& request,
                              unsigned wireBytes, const unsigned char* bytes) {
    SurfaceGuard guard;
    const int result = ValidateWriteLocked(ownerId, request, wireBytes);
    if (result < 0) return result;
    if (!bytes) return GTOS_SURFACE_ERR_BAD_ADDRESS;
    // Validate ALL premultiplied pixels before writing even the first byte.
    for (unsigned i = 0; i < request.length; i += 4)
        if (bytes[i] > bytes[i + 3] || bytes[i + 1] > bytes[i + 3] || bytes[i + 2] > bytes[i + 3])
            return GTOS_SURFACE_ERR_BAD_PIXELS;
    for (unsigned i = 0; i < request.length; ++i) draft[progress + i] = bytes[i];
    progress += request.length;
    return (int)request.length;
}
int NativeSurfaceBank::ValidateControlLocked(unsigned ownerId, const GtosSurfaceControlRequest& request,
                                              unsigned wireBytes) const {
    if (wireBytes != GTOS_SURFACE_CONTROL_REQUEST_BYTES) return GTOS_SURFACE_ERR_BAD_SIZE;
    if (request.version != GTOS_SURFACE_ABI_VERSION) return GTOS_SURFACE_ERR_UNSUPPORTED_VERSION;
    if (!ownerId || !drafting || owner != ownerId || !request.handle || handle != request.handle)
        return GTOS_SURFACE_ERR_BAD_STATE;
    return 0;
}
int NativeSurfaceBank::Present(unsigned ownerId, const GtosSurfaceControlRequest& request, unsigned wireBytes) {
    SurfaceGuard guard;
    const int result = ValidateControlLocked(ownerId, request, wireBytes);
    if (result < 0) return result;
    if (progress != total) return GTOS_SURFACE_ERR_BAD_STATE;
    front.width = width; front.height = height;
    for (unsigned i = 0; i < GTOS_SURFACE_MAX_BYTES; ++i) front.rgba[i] = draft[i];
    front.generation = handle;
    ScrubDraft();
    return 0;
}
int NativeSurfaceBank::Abort(unsigned ownerId, const GtosSurfaceControlRequest& request, unsigned wireBytes) {
    SurfaceGuard guard;
    const int result = ValidateControlLocked(ownerId, request, wireBytes);
    if (result < 0) return result;
    ScrubDraft();
    return 0;
}
void NativeSurfaceBank::ReclaimOwner(unsigned ownerId) {
    SurfaceGuard guard;
    if (ownerId && drafting && owner == ownerId) ScrubDraft();
}
bool NativeSurfaceBank::CopyLatest(unsigned int knownGeneration, gui::NativeImageSnapshot& out) const {
    SurfaceGuard guard;
    if (!front.generation || knownGeneration == front.generation) return false;
    out.generation = front.generation; out.width = front.width; out.height = front.height;
    for (unsigned i = 0; i < GTOS_SURFACE_MAX_BYTES; ++i) out.rgba[i] = front.rgba[i];
    return true;
}

#ifndef GTOS_PROCESS_NATIVE_SURFACE_H
#define GTOS_PROCESS_NATIVE_SURFACE_H
#include <process/surface_abi.h>
#include <gui/native_image.h>

namespace gtos { namespace process {
// One bounded BSP-only draft and one desktop-owned published frame. All
// pixels reside in ordinary trusted supervisor storage; no mapping changes,
// allocations, GUI calls or retained user addresses occur in this service.
class NativeSurfaceBank : public gui::NativeImageProvider {
#ifdef GTOS_SURFACE_HOST_TEST
    // Private test inspection/counter positioning; absent from kernel ABI.
    friend struct NativeSurfaceBankHostAccess;
#endif
    bool available, drafting;
    unsigned owner, handle, width, height, total, progress, nextHandle;
    unsigned char draft[GTOS_SURFACE_MAX_BYTES];
    gui::NativeImageSnapshot front;
    void ScrubDraft();
    int ValidateWriteLocked(unsigned ownerId, const GtosSurfaceWriteRequest&, unsigned wireBytes) const;
    int ValidateControlLocked(unsigned ownerId, const GtosSurfaceControlRequest&, unsigned wireBytes) const;
    NativeSurfaceBank(const NativeSurfaceBank&);
    NativeSurfaceBank& operator=(const NativeSurfaceBank&);
public:
    NativeSurfaceBank();
    static NativeSurfaceBank& Instance();
    // Boot configuration only, before native admission. Runtime availability
    // is set only after the framebuffer desktop was actually constructed.
    void SetAvailable(bool value);
    int Begin(unsigned ownerId, const GtosSurfaceBeginRequest&, unsigned wireBytes);
    int ValidateWrite(unsigned ownerId, const GtosSurfaceWriteRequest&, unsigned wireBytes) const;
    // bytes points to the WHOLE already checked/copied kernel bounce chunk.
    int Write(unsigned ownerId, const GtosSurfaceWriteRequest&, unsigned wireBytes,
              const unsigned char* bytes);
    int Present(unsigned ownerId, const GtosSurfaceControlRequest&, unsigned wireBytes);
    int Abort(unsigned ownerId, const GtosSurfaceControlRequest&, unsigned wireBytes);
    void ReclaimOwner(unsigned ownerId);
    virtual bool CopyLatest(unsigned int knownGeneration, gui::NativeImageSnapshot& out) const;
};
} }
#endif

#ifndef GTOS_GUI_NATIVE_WINDOW_H
#define GTOS_GUI_NATIVE_WINDOW_H

// Host-friendly provider contract for persistent native windows. The desktop
// enumerates at most a fixed number of slots per update and copies whatever it
// needs; no internal buffer pointer ever escapes the provider.
#include <gui/native_image.h>
#include <process/window_abi.h>

namespace gtos { namespace gui {
enum {
    NativeWindowMaximum = 4,
    // 0 live / 1 closing / 2 dead. Lifecycle decides what operations are legal.
    NativeWindowLive = 0,
    NativeWindowClosing = 1,
    NativeWindowDead = 2
};
struct NativeWindowView {
    unsigned handle;           // 0 means the slot is empty
    unsigned width, height;    // CREATE dimensions, valid even before publication
    unsigned frameGeneration;  // 0 means nothing has been published yet
    unsigned lifecycle;
    unsigned large;            // 1 for a desktop-sized window
};
class NativeWindowProvider {
public:
    // False only for an out-of-range index, leaving both outputs untouched.
    // Every in-range index fully defines `view` and `out`.
    virtual bool CopySlot(unsigned index, NativeWindowView& view,
                          NativeImageSnapshot& out) const = 0;
    // A large frame is copied into the caller's backing store by exact identity.
    virtual bool CopyLargeFrame(unsigned, unsigned, unsigned char*, unsigned) const {
        return false;
    }

    // Trusted kernel-side desktop operations. These are NOT reachable from a user
    // system call: the bank resolves the owner from the non-reusable handle under
    // one outer guard and then uses the existing owner-checked paths.
    // live or closing exists; dead or stale is false.
    virtual bool ContainsWindow(unsigned handle) const = 0;
    // 0 delivered, 1 this delivery overflowed and reset, negative not delivered.
    virtual int RouteInput(unsigned handle, const GtosInputEvent& event) = 0;
    // Only a live window; a closing window keeps its pending CLOSE.
    virtual void ResetWindowInput(unsigned handle) = 0;
    // Idempotent desktop request-close; never destroys.
    virtual void RequestWindowClose(unsigned handle) = 0;
protected:
    // Non-owning: the kernel instance must not gain a deleting destructor.
    ~NativeWindowProvider() {}
};
static_assert(sizeof(NativeWindowView) == 24, "Native window view layout");
} }
#endif
